// Iris 数据模型层 —— Trigram 倒排索引
// 支持任意 3+ 字符子串搜索，O(K) 查询（K = 候选集大小）
// 契约：Query 只返回「含全部查询 trigram」的候选集（必要条件，允许假阳性），
//       精确验证由 Matcher::MatchSingle 经 GetSearchText 完成——
//       不预存搜索文本副本（项目硬约束：不存 searchText，路径数据仅在 Pool 存一份）。
// 存储：统一 CSR（keys 升序 + offsets 前缀和 + postings 连续），双后端——
//       堆（构建期/反序列化拷入）或外部视图（快照文件映射，零拷贝）。
//       CSR 消除逐键小容器的分配器开销与碎片，且与快照布局一致可直拷。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace iris {

class IndexStore;

// CSR 借用视图（零拷贝）：keys 升序，offsets.size() = trigramCount+1，
// postings 为全部 posting list 的连续拼接（各 list 升序）。快照布局与之一致。
// 不拥有数据——堆后端指向内部 vector，视图后端指向文件映射；生命周期同所属 TrigramIndex。
struct TrigramCsrView {
    const uint32_t* keys         = nullptr;
    const uint32_t* offsets      = nullptr;
    const uint32_t* postings     = nullptr;
    size_t          trigramCount = 0;
    size_t          postingCount = 0;
};

class TrigramIndex {
public:
    /// 从 IndexStore 构建倒排索引（调用前须 Sort；两遍计数法，峰值最小）
    void Build(const IndexStore& store);

    /// 查询：返回含 pattern 全部 trigram 的候选 entry 索引（已排序去重，允许假阳性）
    std::vector<uint32_t> Query(std::wstring_view pattern) const;

    /// 条目数
    size_t EntryCount() const { return entryCount_; }

    /// 堆内存占用（字节；视图后端返回 0——数据在文件映射中）
    size_t MemoryBytes() const;

    /// 借用 CSR 视图（零拷贝，快照序列化直写用；生命周期同 this）
    TrigramCsrView ExportCsrView() const;
    /// 从 CSR 拷入堆后端（反序列化用；数据完整性由调用方保证）
    void ImportCsr(const uint32_t* keys, const uint32_t* offsets,
                   const uint32_t* postings, size_t trigramCount, size_t entryCount);
    /// 视图后端：CSR 数据在外部存储（文件映射），零拷贝
    void AttachView(const uint32_t* keys, const uint32_t* offsets,
                    const uint32_t* postings, size_t trigramCount, size_t entryCount);

private:
    // CSR 段访问（视图优先于堆）
    struct CsrSegs {
        const uint32_t* keys;
        const uint32_t* offsets;
        const uint32_t* postings;
        size_t          trigramCount;
    };
    CsrSegs Segs() const {
        if (view_.keys != nullptr)
            return {view_.keys, view_.offsets, view_.postings, view_.trigramCount};
        return {csrKeys_.data(), csrOffsets_.data(), csrPostings_.data(), csrKeys_.size()};
    }

    std::vector<uint32_t> csrKeys_;      // 升序
    std::vector<uint32_t> csrOffsets_;   // size = keys+1
    std::vector<uint32_t> csrPostings_;  // 连续拼接

    struct ViewCsr {
        const uint32_t* keys         = nullptr;
        const uint32_t* offsets      = nullptr;
        const uint32_t* postings     = nullptr;
        size_t          trigramCount = 0;
    };
    ViewCsr view_;

    size_t entryCount_ = 0;
};

} // namespace iris
