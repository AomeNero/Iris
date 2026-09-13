// Iris Provider —— 全量重建熔断器
// 只统计"journal 确凿失效"驱动的全量重建（compact 与启动初始化不计数）：
// 滑动窗口内重建次数达上限后拒绝再次触发（索引保持当前状态），
// 旧记录滑出窗口后自然恢复。兜底任何未知原因导致的重建风暴。
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace iris {

class RebuildBreaker {
public:
    RebuildBreaker(size_t maxRebuilds, uint64_t windowMs);

    /// 记录一次已执行的重建。
    void RecordRebuild(uint64_t nowMs);

    /// 当前是否允许再触发一次（不消费额度；触发后由调用方 RecordRebuild）。
    bool ShouldAllow(uint64_t nowMs);

private:
    void Purge(uint64_t nowMs);  // 淘汰滑出窗口的记录

    size_t               maxRebuilds_;
    uint64_t             windowMs_;
    std::deque<uint64_t> rebuildMs_;  // 窗口内的重建时刻（单调递增）
};

} // namespace iris
