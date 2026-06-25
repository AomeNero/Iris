// Iris Provider —— 文件索引（NTFS USN Journal）
// 设计依据: doc/detailed-design.md §6.1
//
// 利用 NTFS USN Journal 实现全磁盘文件毫秒级索引。非 NTFS 卷降级为目录遍历。
// 索引以 CoW 方式更新：搜索时 shared_ptr<const IndexStore> 无锁读，
// Refresh 时构建新 store 后原子替换。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // DWORD / HANDLE（本头文件接口与内部类型使用）

#include "provider/ISearchableProvider.h"
#include "provider/IndexStore.h"

namespace iris {

class FileProvider : public ISearchableProvider {
    Q_OBJECT
public:
    FileProvider();
    ~FileProvider() override;

    // ---- IProvider ----
    bool Initialize() override;
    void Shutdown() override;
    void CancelInitialize() override;
    std::vector<ResultItem> GetAll() const override;
    size_t GetCount() const override;
    std::wstring GetName() const override { return L"FileProvider"; }
    bool IsReady() const override { return ready_.load(std::memory_order_acquire); }
    void Refresh() override;

    // ---- ISearchableProvider ----
    size_t      FindFirstPrefix(std::wstring_view prefix) const override;
    ResultItem  BuildResultItem(size_t index) const override;
    std::wstring GetSearchText(size_t index) const override;
    ItemType    GetType(size_t index) const override;
    uint8_t     GetPathDepth(size_t index) const override;

    // ---- 排除规则（纯函数，供测试）----
    // fileAttributes: USN_RECORD 的 FileAttributes；不调 GetFileAttributesW。
    // excludeHidden/excludeSystem 默认 true（保持旧行为）；由 SetExcludeFlags 按配置覆盖。
    static bool ShouldExclude(DWORD fileAttributes, const std::wstring& fileName,
                              bool excludeHidden = true, bool excludeSystem = true);

    /// 按配置设置排除标志（须在 Initialize 前调用）。
    void SetExcludeFlags(bool excludeHidden, bool excludeSystem);

private:
    struct VolumeInfo {
        std::wstring rootPath;       // 如 "C:\\"
        wchar_t      driveLetter = 0;
        HANDLE       hVolume = INVALID_HANDLE_VALUE;
        uint64_t     lastUsn = 0;     // 上次读取到的 USN 位置
        uint64_t     journalId = 0;
        bool         isNTFS = false;
    };

    // 构建一个卷的索引（全量），填充 store。返回 true 成功。
    bool BuildVolumeIndex(const VolumeInfo& vol, IndexStore& store,
                          std::unordered_map<uint64_t, uint16_t>& dirIdByFrn);

    // 非 NTFS 降级扫描
    void FallbackScan(const VolumeInfo& vol, IndexStore& store,
                      std::unordered_map<uint64_t, uint16_t>& dirIdByFrn);

    // 增量处理一条 USN 记录
    void ApplyUsnRecord(const void* record, const VolumeInfo& vol,
                        IndexStore& store,
                        std::unordered_map<uint64_t, uint16_t>& dirIdByFrn);

    // 排除规则实现在 .cpp（声明见上方 public static ShouldExclude）

    // CoW：取当前索引（无锁拷贝 shared_ptr）
    std::shared_ptr<const IndexStore> Snapshot() const;
    void ReplaceIndex(std::shared_ptr<const IndexStore> next);

    std::shared_ptr<const IndexStore> index_;
    mutable std::mutex                indexSwapMutex_;
    std::vector<VolumeInfo>           volumes_;
    std::atomic<bool>                 ready_{false};
    std::atomic<uint64_t>             totalFiles_{0};
    std::atomic<uint64_t>             totalDirs_{0};
    bool                              excludeHidden_ = true;
    bool                              excludeSystem_ = true;
    std::atomic<bool>                 cancelInitialize_{false};  // 退出时 set，使 Initialize 快速结束
};

} // namespace iris
