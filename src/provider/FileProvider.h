// Iris Provider —— 文件索引（NTFS USN Journal）
// 设计依据: doc/detailed-design.md §6.1
//
// 利用 NTFS USN Journal 实现全磁盘文件毫秒级索引。非 NTFS 卷降级为目录遍历。
// 索引以 CoW 方式更新：搜索时 shared_ptr<const IndexStore> 无锁读，
// Refresh 时构建新 store 后原子替换。
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // DWORD / HANDLE（本头文件接口与内部类型使用）

#include "provider/DeltaOverlay.h"
#include "provider/ISearchableProvider.h"
#include "provider/IndexStore.h"
#include "provider/RebuildBreaker.h"
#include "provider/UsnDelta.h"
#include "provider/UsnWatchPolicy.h"

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
    std::wstring GetPath(size_t index) const override;
    const TrigramIndex* GetTrigramIndex() const override;
    /// 子串搜索：base 候选（过滤已删除）+ delta 新增/改名候选（合并到
    /// index >= baseSize 的 delta 空间）。前缀扫描路径（<3 字符查询）不含 delta。
    std::vector<uint32_t> SearchTrigram(std::wstring_view pattern) const override;

    // ---- 排除规则（纯函数，供测试）----
    // fileAttributes: USN_RECORD 的 FileAttributes；不调 GetFileAttributesW。
    // excludeHidden/excludeSystem 默认 true（保持旧行为）；由 SetExcludeFlags 按配置覆盖。
    static bool ShouldExclude(DWORD fileAttributes, const std::wstring& fileName,
                              bool excludeHidden = true, bool excludeSystem = true);

    /// 按配置设置排除标志（须在 Initialize 前调用）。
    void SetExcludeFlags(bool excludeHidden, bool excludeSystem);

    /// 设置快照文件路径（须在 Initialize 前调用；空 = 不使用快照）。
    /// 有快照：启动加载映射视图秒级可搜，随后后台全量重建校准并重写快照。
    void SetSnapshotPath(std::filesystem::path p) { snapshotPath_ = std::move(p); }

signals:
    /// 所有 NTFS 卷的 USN 监听均不可用（权限不足/句柄不可恢复）→ 索引为静态
    /// 快照。UI 据此提示（托盘 tooltip 等）。
    void usnWatchUnavailable();
    /// journal 失效驱动的全量重建被熔断（窗口内触发过频）。UI 据此气泡告知。
    void rebuildSuspended();

private:
    struct VolumeInfo {
        std::wstring rootPath;       // 如 "C:\\"
        wchar_t      driveLetter = 0;
        HANDLE       hVolume = INVALID_HANDLE_VALUE;
        uint64_t     lastUsn = 0;     // 上次读取到的 USN 位置
        uint64_t     journalId = 0;
        bool         isNTFS = false;
        // —— 监听策略状态（guard: volumesMutex_）——
        bool     usnDisabled       = false;  // 该卷监听已禁用（权限/句柄不可恢复）
        bool     handleFromService = false;  // 卷句柄来自 IrisService（诊断用）
        uint32_t backoffFails      = 0;      // 未知错误连续失败计数
        uint64_t backoffDelayMs = 0;      // 未知错误退避间隔（0 = 不退避）
        uint64_t lastPollMs     = 0;      // 上次尝试轮询时刻（退避计时）
    };

    // 构建一个卷的索引（全量），填充 store。返回 true 成功。
    // 成功时把打开的卷句柄与 USN 游标写回 vol（所有权随 vol 移交 volumes_）。
    bool BuildVolumeIndex(VolumeInfo& vol, IndexStore& store,
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
    // 取当前组合视图（base + delta 快照；与 index_ 同锁 CoW）
    std::shared_ptr<const DeltaOverlay> Overlay() const;

    // —— 全量构建 / 定格切换（Initialize 与 compact 重建共用）——
    std::shared_ptr<IndexStore> BuildFullIndex();          // 取消时返回 nullptr
    void FinalizeAndSwap(std::shared_ptr<IndexStore> store); // 落盘+切视图+清 delta

    // —— USN 实时增量（监听线程；轮询各 NTFS 卷的增量并应用 delta）——
    enum class RebuildReason { JournalInvalid, Compact, Manual };
    enum class ReopenResult { Ok, Failed, JournalChanged };
    void UsnWatchLoop();
    // 调用方须持有 deltaMutex_。返回 0 = 成功（含无增量 EOF），否则 Win32 错误码
    uint32_t ReadVolumeUsnDelta(VolumeInfo& vol);
    // 调用方须持有 volumesMutex_ + deltaMutex_。按分派表处理读取失败；
    // 返回 true = journal 确凿失效（经熔断器全量重建）
    bool HandleUsnReadFailure(VolumeInfo& vol, uint32_t err);
    // 重开卷句柄并重查 journal 身份（vol 句柄/游标随之更新）
    ReopenResult ReopenVolumeHandle(VolumeInfo& vol);
    // 幂等：重建中则忽略。JournalInvalid 经熔断器（超限拒绝并 emit rebuildSuspended）
    void TriggerRebuild(RebuildReason reason);

    std::shared_ptr<const IndexStore> index_;
    mutable std::mutex                indexSwapMutex_;
    std::vector<VolumeInfo>           volumes_;     // guard: volumesMutex_（watch 遍历 vs rebuild 重建）
    mutable std::mutex                volumesMutex_;
    std::filesystem::path             snapshotPath_;
    std::atomic<bool>                 ready_{false};
    std::atomic<uint64_t>             totalFiles_{0};
    std::atomic<uint64_t>             totalDirs_{0};
    bool                              excludeHidden_ = true;
    bool                              excludeSystem_ = true;
    std::atomic<bool>                 cancelInitialize_{false};  // 退出时 set，使 Initialize 快速结束

    // —— USN 实时增量（7b）——
    std::unique_ptr<UsnDelta>           delta_;       // guard: deltaMutex_
    mutable std::mutex                  deltaMutex_;
    std::shared_ptr<const DeltaOverlay> overlay_;     // guard: indexSwapMutex_（随 index_ CoW）
    std::thread                         watchThread_;
    std::thread                         rebuildThread_;
    std::atomic<bool>                   stopWatch_{false};
    std::atomic<bool>                   rebuilding_{false};
    RebuildBreaker                      rebuildBreaker_{3, 60 * 60 * 1000};  // 仅 JournalInvalid 计数
    std::atomic<bool>                   usnUnavailableEmitted_{false};
    std::atomic<bool>                   rebuildSuspendNotified_{false};
};

} // namespace iris
