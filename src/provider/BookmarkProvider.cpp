// Iris Provider —— BookmarkProvider 实现
#include "provider/BookmarkProvider.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "core/StringUtil.h"
#include "core/WinUtil.h"

namespace iris {

namespace {

// Chrome/Edge 的三个根节点
constexpr const char* kRoots[] = {"bookmark_bar", "other", "synced"};

// 一个书签源文件 + 来源浏览器
struct BookmarkSource {
    std::filesystem::path path;
    bool                  isEdge;  // false=Chrome, true=Edge
};

// 扫描某浏览器所有 Profile 目录下的 Bookmarks 文件
// userDataDir 形如 .../Google/Chrome/User Data
void CollectSources(const std::filesystem::path& userDataDir, bool isEdge,
                    std::vector<BookmarkSource>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(userDataDir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(userDataDir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        const auto bmPath = entry.path() / L"Bookmarks";
        if (std::filesystem::exists(bmPath, ec)) out.push_back({bmPath, isEdge});
    }
}

// 取字符串字段（缺失/类型不符返回空）
std::wstring GetStr(const nlohmann::json& node, const char* key) {
    const auto it = node.find(key);
    if (it == node.end() || !it->is_string()) return {};
    return StringUtil::Utf8ToWide(it->get<std::string>());
}

// 递归解析书签节点：url 节点收集，folder 节点拼层级后递归 children
void ParseNode(const nlohmann::json& node,
               const std::wstring& parentPath,
               int depth, bool isEdge,
               std::vector<BookmarkProvider::Entry>& out) {
    if (!node.is_object()) return;

    const std::string type = node.value("type", std::string{});
    if (type == "url") {
        BookmarkProvider::Entry e;
        e.title   = GetStr(node, "name");
        e.url     = GetStr(node, "url");
        e.subtitle = parentPath;
        e.depth   = static_cast<uint8_t>(depth);
        e.isEdge  = isEdge;
        if (!e.title.empty() && !e.url.empty()) out.push_back(std::move(e));
        return;
    }

    // folder / roots 层：拼当前节点名到层级，再递归 children
    std::wstring curPath = parentPath;
    const auto nameIt = node.find("name");
    if (nameIt != node.end() && nameIt->is_string()) {
        if (!curPath.empty()) curPath += L" / ";
        curPath += StringUtil::Utf8ToWide(nameIt->get<std::string>());
    }

    const auto childrenIt = node.find("children");
    if (childrenIt != node.end() && childrenIt->is_array()) {
        for (const auto& child : *childrenIt) {
            ParseNode(child, curPath, depth + 1, isEdge, out);
        }
    }
}

} // namespace

BookmarkProvider::BookmarkProvider() = default;

BookmarkProvider::~BookmarkProvider() { Shutdown(); }

bool BookmarkProvider::Initialize() {
    if (ready_.load(std::memory_order_acquire)) return true;
    const bool ok = Rebuild();
    ready_.store(ok, std::memory_order_release);
    if (ok) emit dataChanged();
    return ok;
}

void BookmarkProvider::Shutdown() {
    ReplaceEntries(nullptr);
    ready_.store(false, std::memory_order_release);
}

void BookmarkProvider::Refresh() {
    if (Rebuild()) {
        ready_.store(true, std::memory_order_release);
        emit dataChanged();
    }
}

bool BookmarkProvider::Rebuild() {
    const std::wstring localAppData = WinUtil::GetKnownFolderPath(FOLDERID_LocalAppData);
    if (localAppData.empty()) return false;

    std::vector<BookmarkSource> sources;
    const std::filesystem::path base(localAppData);
    CollectSources(base / L"Google\\Chrome\\User Data", false, sources);
    CollectSources(base / L"Microsoft\\Edge\\User Data", true, sources);

    std::vector<Entry> entries;
    for (const auto& src : sources) {
        std::ifstream ifs(src.path);
        if (!ifs) continue;
        const std::string text((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
        const size_t before = entries.size();
        ParseBookmarksText(text, src.isEdge, entries);
        if (entries.size() == before) {
            IRIS_LOG_WARN(L"BookmarkProvider: 解析 " + src.path.wstring() + L" 无结果");
        }
    }

    // 按 title 字典序（CI）排序——Matcher 前缀扫描早停所必需
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0;
              });

    ReplaceEntries(std::make_shared<const std::vector<Entry>>(std::move(entries)));
    return true;
}

void BookmarkProvider::ParseBookmarksText(const std::string& utf8Json, bool isEdge,
                                          std::vector<Entry>& out) {
    try {
        const nlohmann::json j = nlohmann::json::parse(utf8Json);
        const auto roots = j.find("roots");
        if (roots == j.end() || !roots->is_object()) return;
        for (const char* root : kRoots) {
            const auto it = roots->find(root);
            if (it != roots->end() && it->is_object()) {
                ParseNode(*it, L"", 0, isEdge, out);
            }
        }
    } catch (const std::exception& e) {
        IRIS_LOG_WARN(L"BookmarkProvider: JSON 解析失败: " +
                      StringUtil::Utf8ToWide(e.what()));
    }
}

std::shared_ptr<const std::vector<BookmarkProvider::Entry>>
BookmarkProvider::Snapshot() const {
    std::lock_guard<std::mutex> lk(swapMutex_);
    return entries_;
}

void BookmarkProvider::ReplaceEntries(std::shared_ptr<const std::vector<Entry>> next) {
    std::lock_guard<std::mutex> lk(swapMutex_);
    entries_ = std::move(next);
}

std::vector<ResultItem> BookmarkProvider::GetAll() const {
    const auto e = Snapshot();
    std::vector<ResultItem> out;
    if (!e) return out;
    out.reserve(e->size());
    for (size_t i = 0; i < e->size(); ++i) out.push_back(BuildResultItem(i));
    return out;
}

size_t BookmarkProvider::GetCount() const {
    const auto e = Snapshot();
    return e ? e->size() : 0;
}

size_t BookmarkProvider::FindFirstPrefix(std::wstring_view prefix) const {
    const auto e = Snapshot();
    if (!e || prefix.empty()) return 0;
    const std::wstring p(prefix);
    for (size_t i = 0; i < e->size(); ++i) {
        if (_wcsicmp((*e)[i].title.c_str(), p.c_str()) >= 0) return i;
    }
    return e->size();
}

ResultItem BookmarkProvider::BuildResultItem(size_t index) const {
    ResultItem r;
    const auto e = Snapshot();
    if (!e || index >= e->size()) return r;
    const Entry& en = (*e)[index];
    r.title     = en.title;
    r.subtitle  = en.subtitle;
    r.path      = en.url;
    r.type      = ItemType::BOOKMARK;
    r.pathDepth = en.depth;
    r.source    = en.isEdge ? 1 : 0;
    return r;
}

std::wstring BookmarkProvider::GetSearchText(size_t index) const {
    const auto e = Snapshot();
    if (!e || index >= e->size()) return {};
    const Entry& en = (*e)[index];
    return en.title + L" " + en.url;
}

ItemType BookmarkProvider::GetType(size_t index) const {
    const auto e = Snapshot();
    if (!e || index >= e->size()) return ItemType::FILE;
    return ItemType::BOOKMARK;
}

uint8_t BookmarkProvider::GetPathDepth(size_t index) const {
    const auto e = Snapshot();
    if (!e || index >= e->size()) return 0;
    return (*e)[index].depth;
}

} // namespace iris
