// Iris Provider —— 应用程序索引（开始菜单 / 桌面 .lnk + UWP）
// 设计依据: doc/detailed-design.md §6.3
//
// 扫描开始菜单与桌面的 .lnk 快捷方式（WinUtil::ResolveShortcut 解析目标），
// 按应用名去重后线性存储。条目少（数百），用 vector<Entry> + 按 title 排序
// （仿 mock_provider.h），不使用 IndexStore/CompactFileEntry（同 BookmarkProvider 理由）。
// UWP 枚举暂为 stub（WinUtil::EnumerateUwpApps 当前返回空），不影响 .lnk 搜索。
// CoW：搜索时无锁读，Refresh 重建后原子替换。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "provider/ISearchableProvider.h"

namespace iris {

class AppProvider : public ISearchableProvider {
    Q_OBJECT
public:
    // 单条应用（public：供 .cpp 与单元测试引用）
    struct Entry {
        std::wstring title;       // 应用名（.lnk 文件名去扩展名）
        std::wstring targetPath;  // 快捷方式目标 exe 路径（打开用）
        std::wstring pinyinFull;      // title 的无音调全拼（拼音匹配用，Rebuild 预计算）
        std::wstring pinyinInitials;  // title 的首字母
    };

    AppProvider();
    ~AppProvider() override;

    // ---- IProvider ----
    bool Initialize() override;
    void Shutdown() override;
    std::vector<ResultItem> GetAll() const override;
    size_t GetCount() const override;
    std::wstring GetName() const override { return L"AppProvider"; }
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

    // 按应用名（CI）去重，保留先出现（开始菜单优先于桌面）。纯函数，供测试。
    static void DeduplicateByName(std::vector<Entry>& entries);

private:
    std::shared_ptr<const std::vector<Entry>> Snapshot() const;
    void ReplaceEntries(std::shared_ptr<const std::vector<Entry>> next);

    // 全量构建（Initialize 与 Refresh 共用）
    bool Rebuild();
    // 递归扫描一个目录下所有 .lnk（跳过无权限子目录）
    void ScanDirectory(const std::wstring& dir, std::vector<Entry>& out) const;

    std::shared_ptr<const std::vector<Entry>> entries_;
    mutable std::mutex swapMutex_;
    std::atomic<bool> ready_{false};
};

} // namespace iris
