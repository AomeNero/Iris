// Iris Provider —— 浏览器书签索引（Chrome / Edge）
// 设计依据: doc/detailed-design.md §6.2
//
// 解析 Chrome/Edge 的 Bookmarks JSON（所有 Profile），递归收集 url 书签。
// 条目少（数百级），用 vector<Entry> 线性存储 + 按 title 字典序排序（仿
// mock_provider.h），不使用 IndexStore/CompactFileEntry——后者为文件级百万
// 条目二分而设计，且 detailed-design §6.2 伪代码里的 CompactFileEntry 字段
// (nameLength/flags/usnLow/dirId) 与实际 IndexStore.h 的 14B 位域版不符。
// CoW：搜索时 shared_ptr<const vector<Entry>> 无锁读，Refresh 重建后原子替换。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "provider/ISearchableProvider.h"

namespace iris {

class BookmarkProvider : public ISearchableProvider {
    Q_OBJECT
public:
    // 单条书签（public：供 .cpp 解析函数与单元测试引用）
    struct Entry {
        std::wstring title;     // 书签名
        std::wstring subtitle;  // 文件夹层级，如 "书签栏 / 工作"
        std::wstring url;       // 完整 URL（打开用）
        uint8_t     depth = 0;  // 文件夹嵌套深度
        bool        isEdge = false;  // 来源浏览器：false=Chrome, true=Edge
        std::wstring pinyinFull;      // title 的无音调全拼（拼音匹配用，Rebuild 预计算）
        std::wstring pinyinInitials;  // title 的首字母
    };

    // 解析单个 Bookmarks JSON 文本（UTF-8），收集条目（未排序）。供 Rebuild 与测试共用。
    // isEdge 标记来源浏览器，写入每条 Entry（UI 据此显示对应浏览器图标）。
    static void ParseBookmarksText(const std::string& utf8Json, bool isEdge, std::vector<Entry>& out);

    BookmarkProvider();
    ~BookmarkProvider() override;

    // ---- IProvider ----
    bool Initialize() override;
    void Shutdown() override;
    std::vector<ResultItem> GetAll() const override;
    size_t GetCount() const override;
    std::wstring GetName() const override { return L"BookmarkProvider"; }
    bool IsReady() const override { return ready_.load(std::memory_order_acquire); }
    void Refresh() override;

    // ---- ISearchableProvider ----
    size_t FindFirstPrefix(std::wstring_view prefix) const override;
    ResultItem BuildResultItem(size_t index) const override;
    std::wstring GetSearchText(size_t index) const override;
    std::wstring GetPinyinFull(size_t index) const override;
    std::wstring GetPinyinInitials(size_t index) const override;
    ItemType GetType(size_t index) const override;
    uint8_t GetPathDepth(size_t index) const override;

private:
    // CoW：Snapshot 拷贝 shared_ptr（加锁仅保护指针交换，读无锁）
    std::shared_ptr<const std::vector<Entry>> Snapshot() const;
    void ReplaceEntries(std::shared_ptr<const std::vector<Entry>> next);

    // 全量构建（Initialize 与 Refresh 共用）
    bool Rebuild();

    std::shared_ptr<const std::vector<Entry>> entries_;
    mutable std::mutex swapMutex_;
    std::atomic<bool> ready_{false};
};

} // namespace iris
