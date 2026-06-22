// Iris Provider —— AppProvider 实现
#include "provider/AppProvider.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>

#include "core/Logger.h"
#include "core/PinyinUtil.h"
#include "core/StringUtil.h"
#include "core/WinUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>   // CoInitializeEx / CoUninitialize / RPC_E_CHANGED_MODE

namespace iris {

namespace {

// 文件名是否以 .lnk 结尾（CI）。注意 ".lnk" 是 4 字符。
bool EndsWithLnk(const std::wstring& name) {
    if (name.size() < 4) return false;
    return _wcsicmp(name.c_str() + name.size() - 4, L".lnk") == 0;
}

} // namespace

AppProvider::AppProvider() = default;

AppProvider::~AppProvider() { Shutdown(); }

bool AppProvider::Initialize() {
    if (ready_.load(std::memory_order_acquire)) return true;
    const bool ok = Rebuild();
    ready_.store(ok, std::memory_order_release);
    if (ok) emit dataChanged();
    return ok;
}

void AppProvider::Shutdown() {
    ReplaceEntries(nullptr);
    ready_.store(false, std::memory_order_release);
}

void AppProvider::Refresh() {
    if (Rebuild()) {
        ready_.store(true, std::memory_order_release);
        emit dataChanged();
    }
}

bool AppProvider::Rebuild() {
    // WinUtil::ResolveShortcut 走 IShellLink COM。对称配对（同线程 CoInit/CoUninit），
    // 使本方法无论由 Initialize 的索引线程还是 Refresh 的 UI 线程调用都能正确配对。
    // S_OK / S_FALSE 均算成功（S_FALSE=本线程已同模型初始化），需配对 CoUninitialize。
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool didInit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        IRIS_LOG_WARN(L"AppProvider: CoInitializeEx 失败 hr=0x" +
                      std::to_wstring(static_cast<unsigned long>(hr)));
    }

    std::vector<Entry> entries;
    // 开始菜单优先（用户 > 公共），桌面次之（用户 > 公共）
    ScanDirectory(WinUtil::GetKnownFolderPath(FOLDERID_Programs), entries);
    ScanDirectory(WinUtil::GetKnownFolderPath(FOLDERID_CommonPrograms), entries);
    ScanDirectory(WinUtil::GetKnownFolderPath(FOLDERID_Desktop), entries);
    ScanDirectory(WinUtil::GetKnownFolderPath(FOLDERID_PublicDesktop), entries);

    // UWP（暂为 stub，返回空）；纳入后由去重/排序统一处理
    for (const auto& uwp : WinUtil::EnumerateUwpApps()) {
        if (uwp.name.empty()) continue;
        Entry e;
        e.title = uwp.name;
        e.targetPath = uwp.appUserModelId;  // AUMID 用于启动
        entries.push_back(std::move(e));
    }

    DeduplicateByName(entries);

    // 按 title 字典序（CI）排序——Matcher 前缀扫描早停所必需
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
              });

    // 预计算拼音（拼音匹配用；App 条目少，瞬间完成）
    for (auto& e : entries) {
        e.pinyinFull = PinyinUtil::ToFull(e.title);
        e.pinyinInitials = PinyinUtil::ToInitials(e.title);
    }

    IRIS_LOG_INFO(L"AppProvider: 索引完成，应用数=" + std::to_wstring(entries.size()));

    ReplaceEntries(std::make_shared<const std::vector<Entry>>(std::move(entries)));

    if (didInit) CoUninitialize();  // 与 Rebuild 开头的 CoInitializeEx 同线程配对
    return true;
}

void AppProvider::ScanDirectory(const std::wstring& dir, std::vector<Entry>& out) const {
    if (dir.empty()) return;
    const std::filesystem::path p(dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec)) {
        IRIS_LOG_WARN(L"AppProvider: 目录不存在: " + dir);
        return;
    }

    int lnkFound = 0, resolved = 0;
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& it : std::filesystem::recursive_directory_iterator(p, opts, ec)) {
        if (ec) break;
        if (it.is_directory(ec)) continue;
        const std::wstring lnkPath = it.path().wstring();
        if (!EndsWithLnk(lnkPath)) continue;
        ++lnkFound;

        const WinUtil::ShortcutInfo si = WinUtil::ResolveShortcut(lnkPath);
        if (si.targetPath.empty()) continue;  // 无效快捷方式
        ++resolved;

        Entry e;
        // title 取 .lnk 文件名去扩展名（".lnk" 是 4 字符）
        std::wstring fname = it.path().filename().wstring();
        if (fname.size() >= 4) fname.resize(fname.size() - 4);
        e.title = fname;
        e.targetPath = si.targetPath;
        out.push_back(std::move(e));
    }

    IRIS_LOG_INFO(L"AppProvider: " + dir + L"  lnk=" + std::to_wstring(lnkFound) +
                  L"  resolved=" + std::to_wstring(resolved));
}

void AppProvider::DeduplicateByName(std::vector<Entry>& entries) {
    std::unordered_set<std::wstring> seen;
    std::vector<Entry> deduped;
    deduped.reserve(entries.size());
    for (auto& e : entries) {
        //ToLower 已对 ASCII 区小写化；作为去重 key 足够
        const std::wstring key = StringUtil::ToLower(e.title);
        if (seen.insert(key).second) {
            deduped.push_back(std::move(e));
        }
        // 已存在则丢弃（保留先出现：开始菜单优先于桌面）
    }
    entries = std::move(deduped);
}

std::shared_ptr<const std::vector<AppProvider::Entry>>
AppProvider::Snapshot() const {
    std::lock_guard<std::mutex> lk(swapMutex_);
    return entries_;
}

void AppProvider::ReplaceEntries(std::shared_ptr<const std::vector<Entry>> next) {
    std::lock_guard<std::mutex> lk(swapMutex_);
    entries_ = std::move(next);
}

std::vector<ResultItem> AppProvider::GetAll() const {
    const auto e = Snapshot();
    std::vector<ResultItem> out;
    if (!e) return out;
    out.reserve(e->size());
    for (size_t i = 0; i < e->size(); ++i) out.push_back(BuildResultItem(i));
    return out;
}

size_t AppProvider::GetCount() const {
    const auto e = Snapshot();
    return e ? e->size() : 0;
}

size_t AppProvider::FindFirstPrefix(std::wstring_view prefix) const {
    const auto e = Snapshot();
    if (!e || prefix.empty()) return 0;
    const std::wstring p(prefix);
    for (size_t i = 0; i < e->size(); ++i) {
        if (_wcsicmp((*e)[i].title.c_str(), p.c_str()) >= 0) return i;
    }
    return e->size();
}

ResultItem AppProvider::BuildResultItem(size_t index) const {
    ResultItem r;
    const auto e = Snapshot();
    if (!e || index >= e->size()) return r;
    const Entry& en = (*e)[index];
    r.title     = en.title;
    r.subtitle  = en.targetPath;
    r.path      = en.targetPath;
    r.type      = ItemType::APPLICATION;
    r.pathDepth = 0;
    return r;
}

std::wstring AppProvider::GetSearchText(size_t index) const {
    const auto e = Snapshot();
    if (!e || index >= e->size()) return {};
    const Entry& en = (*e)[index];
    return en.title + L" " + en.targetPath;
}

std::wstring AppProvider::GetPinyinFull(size_t index) const {
    const auto e = Snapshot();
    return (e && index < e->size()) ? (*e)[index].pinyinFull : std::wstring{};
}

std::wstring AppProvider::GetPinyinInitials(size_t index) const {
    const auto e = Snapshot();
    return (e && index < e->size()) ? (*e)[index].pinyinInitials : std::wstring{};
}

ItemType AppProvider::GetType(size_t /*index*/) const {
    return ItemType::APPLICATION;
}

uint8_t AppProvider::GetPathDepth(size_t /*index*/) const {
    return 0;
}

} // namespace iris
