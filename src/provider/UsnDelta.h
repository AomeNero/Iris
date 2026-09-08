// Iris Provider 层 —— USN 内存增量层
// mmap 快照之外的小变更集合（新建/删除/改名/属性变化），搜索时与基础索引
// 合并；达到阈值（条数或滞留时长）触发 compact：重建 → 原子落盘 → swap。
// 纯内存逻辑，不涉及卷 I/O；线程不安全（调用方保证并发安全）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "provider/UsnRecordParser.h"

namespace iris {

// USN_REASON_* 关键位（值与 winioctl.h 一致；本地定义避免头文件传染 windows.h）
inline constexpr uint32_t kUsnReasonFileCreate = 0x00000100;
inline constexpr uint32_t kUsnReasonFileDelete = 0x00000200;
inline constexpr uint32_t kUsnReasonRenameOld  = 0x00001000;
inline constexpr uint32_t kUsnReasonRenameNew  = 0x00002000;
inline constexpr uint32_t kUsnReasonBasicInfo  = 0x00008000;

class UsnDelta {
public:
    enum class Kind { Upsert, Remove };

    struct Change {
        Kind         kind = Kind::Upsert;
        uint64_t     frn = 0;
        uint64_t     parentFrn = 0;
        std::wstring name;
        uint32_t     attributes = 0;
    };

    struct Policy {
        size_t   maxChanges = 5000;          // 条数阈值
        uint64_t maxAgeMs = 10 * 60 * 1000;  // 存在变更时的最长滞留
    };

    explicit UsnDelta(Policy policy = {}) : policy_(policy) {}

    /// 应用一条 USN 事件（nowMs：单调毫秒，用于滞留计时）。
    /// 规则：DELETE→Remove（同帧内新建又删除净零）；CREATE→Upsert；
    ///      RENAME_OLD→记 pending；RENAME_NEW→新名 Upsert（含无 pending 的移入）；
    ///      BASIC_INFO→Upsert 更新属性；DATA_OVERWRITE/CLOSE 等其余→忽略。
    void Apply(const UsnEvent& e, uint64_t nowMs) {
        const uint32_t r = e.reason;
        if (r & kUsnReasonFileDelete) {
            Remove(nowMs, e.frn);
            pendingRename_.erase(e.frn);
            return;
        }
        if (r & kUsnReasonFileCreate) { Upsert(nowMs, e); return; }
        if (r & kUsnReasonRenameOld) { pendingRename_[e.frn] = e.name; return; }
        if (r & kUsnReasonRenameNew) {
            Upsert(nowMs, e);
            pendingRename_.erase(e.frn);
            return;
        }
        if (r & kUsnReasonBasicInfo) { Upsert(nowMs, e); return; }
        // 其余 reason（DATA_OVERWRITE / CLOSE / STREAM_CHANGE 等）→ 忽略
    }

    size_t Size() const { return changes_.size(); }
    bool   Empty() const { return changes_.empty(); }
    const std::unordered_map<uint64_t, Change>& Items() const { return changes_; }

    /// 是否该触发 compact：条数达标，或最早变更滞留超过时限（空 delta 永不触发）
    bool ShouldCompact(uint64_t nowMs) const {
        if (changes_.size() >= policy_.maxChanges) return true;
        if (!changes_.empty() && firstChangeMs_ != 0 &&
            nowMs - firstChangeMs_ >= policy_.maxAgeMs) return true;
        return false;
    }

    void Clear() {
        changes_.clear();
        pendingRename_.clear();
        firstChangeMs_ = 0;
    }

private:
    void Touch(uint64_t nowMs) {
        if (firstChangeMs_ == 0) firstChangeMs_ = nowMs;
    }
    void Upsert(uint64_t nowMs, const UsnEvent& e) {
        Touch(nowMs);
        changes_[e.frn] = Change{Kind::Upsert, e.frn, e.parentFrn, e.name, e.attributes};
    }
    void Remove(uint64_t nowMs, uint64_t frn) {
        Touch(nowMs);
        const auto it = changes_.find(frn);
        if (it != changes_.end() && it->second.kind == Kind::Upsert) {
            changes_.erase(it);  // 本 delta 内新建又删除 → 净零
            return;
        }
        changes_[frn] = Change{Kind::Remove, frn, 0, {}, 0};
    }

    Policy policy_;
    std::unordered_map<uint64_t, Change> changes_;             // frn → 最新净变更
    std::unordered_map<uint64_t, std::wstring> pendingRename_; // frn → 旧名（RENAME_OLD）
    uint64_t firstChangeMs_ = 0;
};

} // namespace iris
