// Iris UI —— 图标缓存实现
#include "ui/IconCache.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

namespace iris {

namespace {

// 内嵌真实图标的扩展名（小写，不含点）：按完整路径缓存 + 真实文件提取
constexpr std::array<std::wstring_view, 5> kIconBearingExts = {
    L"exe", L"lnk", L"ico", L"msi", L"scr"};

std::wstring ToLowerCopy(std::wstring_view s) {
    std::wstring out(s);
    for (auto& ch : out)
        ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(ch)));
    return out;
}

// 取扩展名（小写；无扩展名返回空；目录部分的点不算）
std::wstring LowerExtension(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot   = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return {};
    return ToLowerCopy(path.substr(dot + 1));
}

} // namespace

bool HasIconBearingExtension(const std::wstring& path) {
    const std::wstring ext = LowerExtension(path);
    return std::find(kIconBearingExts.begin(), kIconBearingExts.end(),
                     std::wstring_view(ext)) != kIconBearingExts.end();
}

std::wstring IconKey(const std::wstring& path) {
    if (path.empty()) return {};
    const std::wstring ext = LowerExtension(path);
    if (ext.empty()) return {};  // 无扩展名共享兜底
    if (std::find(kIconBearingExts.begin(), kIconBearingExts.end(),
                  std::wstring_view(ext)) != kIconBearingExts.end()) {
        return ToLowerCopy(path);  // 真实图标 per-file：全路径（小写）为 key
    }
    return ext;  // 其余：扩展名共享
}

IconCache::IconCache(Loader loader, size_t capacity)
    : loader_(std::move(loader)), cache_(capacity) {}

QPixmap IconCache::Get(const std::wstring& path) {
    if (path.empty()) return {};
    const std::wstring key = IconKey(path);
    if (auto hit = cache_.Get(key)) return *hit;
    QPixmap pm = loader_ ? loader_(path) : QPixmap();
    cache_.Put(key, std::move(pm));
    return cache_.Get(key).value_or(QPixmap());
}

} // namespace iris
