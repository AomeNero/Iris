// Iris Provider 层 —— 基础索引 + USN delta 的组合视图
// 统一 entry 空间：[0, baseSize) = 基础索引（堆或 mmap 视图），
// [baseSize, baseSize+upsertCount) = delta 新增/改名条目（按 frn 升序稳定排列）。
// Remove 的 delta 条目不占空间——基础条目经 IsRemoved 隐藏（FRN 低 32 位
// 近似匹配，与 CompactFileEntry::usnLow 的既有约定一致）。
// v1 限制：delta 条目搜索文本仅含 name（父目录 FRN 无路径映射，compact 后补全）。
// 生命周期：构造时拷出 upsert/removed 集合，之后不引用 delta（调用方锁策略自由）。
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "provider/IndexStore.h"
#include "provider/UsnDelta.h"

namespace iris {

class DeltaOverlay {
public:
    DeltaOverlay(std::shared_ptr<const IndexStore> base, const UsnDelta& delta)
        : base_(std::move(base)) {
        upserts_.reserve(delta.Size());
        removedLow_.reserve(delta.Size());
        for (const auto& [frn, c] : delta.Items()) {
            if (c.kind == UsnDelta::Kind::Upsert) {
                upserts_.push_back(c);
            } else {
                removedLow_.insert(static_cast<uint32_t>(frn & 0xFFFFFFFFu));
            }
        }
        // frn 升序稳定排列（同 pattern 多次查询的候选顺序一致）
        std::sort(upserts_.begin(), upserts_.end(),
                  [](const UsnDelta::Change& a, const UsnDelta::Change& b) {
                      return a.frn < b.frn;
                  });
    }

    size_t BaseSize() const { return base_ ? base_->size() : 0; }
    size_t Size() const { return BaseSize() + upserts_.size(); }
    // 基础索引指针（读接口按 index 范围分派时保证与候选同一代际）
    const IndexStore* Base() const { return base_.get(); }

    bool IsDelta(size_t idx) const { return idx >= BaseSize(); }
    // idx 为 delta 空间相对下标（idx - BaseSize()）；越界行为未定义（调用方保证）
    const UsnDelta::Change& DeltaChange(size_t idx) const { return upserts_[idx]; }

    bool IsRemoved(size_t baseIdx) const {
        if (!base_) return false;
        return removedLow_.count((*base_)[baseIdx].usnLow) != 0;
    }

    // delta 条目中搜索文本（name）含 pattern（须已小写化）的相对下标列表
    std::vector<uint32_t> DeltaSubstringCandidates(std::wstring_view pattern) const {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < upserts_.size(); ++i) {
            if (ContainsCI(upserts_[i].name, pattern)) out.push_back(static_cast<uint32_t>(i));
        }
        return out;
    }

private:
    static bool ContainsCI(std::wstring_view text, std::wstring_view kw) {
        if (kw.empty() || kw.size() > text.size()) return false;
        for (size_t i = 0; i + kw.size() <= text.size(); ++i) {
            if (_wcsnicmp(text.data() + i, kw.data(), kw.size()) == 0) return true;
        }
        return false;
    }

    std::shared_ptr<const IndexStore> base_;
    std::vector<UsnDelta::Change> upserts_;
    std::unordered_set<uint32_t> removedLow_;
};

} // namespace iris
