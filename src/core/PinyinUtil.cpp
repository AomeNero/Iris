// Iris Core —— PinyinUtil 实现（cpp-pinyin 封装）
#include "core/PinyinUtil.h"

#include "core/StringUtil.h"  // WideToUtf8 / Utf8ToWide

#include <cpp-pinyin/G2pglobal.h>   // Pinyin::setDictionaryPath
#include <cpp-pinyin/ManTone.h>     // Pinyin::ManTone::Style::NORMAL
#include <cpp-pinyin/Pinyin.h>      // Pinyin::Pinyin
#include <cpp-pinyin/PinyinRes.h>   // PinyinResVector / PinyinRes

#include <QDir>
#include <QFile>
#include <filesystem>
#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // EXCEPTION_EXECUTE_HANDLER（SEH 兜底 hanziToPinyin 崩溃）

namespace iris::PinyinUtil {

namespace {

std::unique_ptr<::Pinyin::Pinyin> g_g2p;

// 文本是否适合交给 cpp-pinyin：须含基本汉字 [0x4E00-0x9FFF]（cpp-pinyin 唯一支持范围），
// 且不含代理对 / CJK扩展 / emoji 等——实测这些字符会让 cpp-pinyin 触发栈缓冲区溢出崩溃。
bool IsSafeForPinyin(std::wstring_view text) {
    bool hasBasicHan = false;
    for (wchar_t c : text) {
        const unsigned u = static_cast<unsigned>(c);
        if (u >= 0x4E00 && u <= 0x9FFF) { hasBasicHan = true; continue; }
        if (u < 0x80) continue;                       // ASCII（字母/数字/标点/空格）
        if (u >= 0x3000 && u <= 0x303F) continue;     // CJK 标点（，。、等）
        if (u >= 0xFF00 && u <= 0xFFEF) continue;     // 全角字符
        return false;  // 代理对 / CJK扩展 / 兼容汉字 / emoji / 其他脚本 → 不安全，跳过
    }
    return hasBasicHan;
}

// 实际调用 cpp-pinyin（独立函数：__try 所在函数不能有需栈展开的 C++ 对象，否则 C2712）。
// 同时填充全拼连接 outFull 与首字母连接 outInitials。
void DoHanziToPinyin(const std::string& utf8, std::string& outFull, std::string& outInitials) {
    const auto res = g_g2p->hanziToPinyin(utf8, ::Pinyin::ManTone::Style::NORMAL,
                                          ::Pinyin::Default, false, false, false);
    outFull.clear();
    outInitials.clear();
    for (const auto& r : res) {
        outFull += r.pinyin;  // 非汉字 Error::Default 原样保留
        if (!r.pinyin.empty()) outInitials.push_back(r.pinyin[0]);
    }
}

// SEH 兜底：cpp-pinyin 对个别输入会触发 STATUS_STACK_BUFFER_OVERRUN，C++ try/catch 抓不住；
// 用 __try/__except 捕获，崩溃则返回 false（调用方回退用原文，该条目拼音搜索不可用）。
bool HanziToPinyinSafe(const std::string& utf8, std::string& outFull, std::string& outInitials) {
    __try {
        DoHanziToPinyin(utf8, outFull, outInitials);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool Init(const std::wstring& /*dataDir*/) {
    // 从 qrc 内嵌 dict 注入 cpp-pinyin（内存加载，不落盘）；DictUtil::openStream 命中 g_dictData
    const QStringList files =
        QDir(QStringLiteral(":/pinyin_dict/mandarin")).entryList(QDir::Files);
    for (const QString& f : files) {
        QFile qrc(QStringLiteral(":/pinyin_dict/mandarin/") + f);
        if (qrc.open(QIODevice::ReadOnly)) {
            const QByteArray ba = qrc.readAll();
            ::Pinyin::setDictData("mandarin/" + f.toStdString(),
                                  std::string(ba.constData(), ba.size()));
        }
    }
    try {
        ::Pinyin::setDictionaryPath(std::filesystem::path());  // 内存优先；path 仅 fallback 用
        g_g2p = std::make_unique<::Pinyin::Pinyin>();  // init → openStream 内存命中
    } catch (...) {
        g_g2p.reset();
        return false;
    }
    return g_g2p != nullptr;
}

std::wstring ToFull(std::wstring_view text) {
    if (!g_g2p || text.empty() || !IsSafeForPinyin(text)) return std::wstring(text);
    std::string outFull, outInitials;
    if (!HanziToPinyinSafe(StringUtil::WideToUtf8(text), outFull, outInitials))
        return std::wstring(text);  // cpp-pinyin 崩溃 → 回退原文
    return StringUtil::Utf8ToWide(outFull);
}

std::wstring ToInitials(std::wstring_view text) {
    if (!g_g2p || text.empty() || !IsSafeForPinyin(text)) return std::wstring(text);
    std::string outFull, outInitials;
    if (!HanziToPinyinSafe(StringUtil::WideToUtf8(text), outFull, outInitials))
        return std::wstring(text);
    return StringUtil::Utf8ToWide(outInitials);
}

} // namespace iris::PinyinUtil
