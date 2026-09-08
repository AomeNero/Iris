// Iris 数据模型层 —— Trigram 倒排索引实现（纯 CSR 存储）
#include "provider/TrigramIndex.h"
#include "provider/IndexStore.h"

#include <algorithm>
#include <cwchar>
#include <unordered_map>
#include <unordered_set>

namespace iris {

namespace {

// 将 3 个 wchar_t 打包为 uint32_t（低 16+8+8 位）
uint32_t PackTrigram(wchar_t a, wchar_t b, wchar_t c) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(a)) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(c)));
}

std::wstring ToLower(std::wstring_view s) {
    std::wstring result(s);
    for (auto& ch : result) {
        ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(ch)));
    }
    return result;
}

void ExtractTrigrams(std::wstring_view text, std::unordered_set<uint32_t>& out) {
    if (text.size() < 3) return;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        out.insert(PackTrigram(text[i], text[i + 1], text[i + 2]));
    }
}

// 第 i 条目的搜索文本（lowercase(name + " " + dir)）的 trigram 集合。
// 文本临时拼接用后即弃，不预存副本（路径数据仅在 NamePool/DirPool 存一份）。
void ExtractEntryTrigrams(const IndexStore& store, size_t i,
                          std::unordered_set<uint32_t>& out) {
    const auto& entry = store[i];
    auto name = store.NameAt(entry);
    auto dir  = store.DirOf(entry);
    std::wstring text;
    text.reserve(name.size() + 1 + dir.size());
    text.assign(name);
    text += L' ';
    text.append(dir);
    out.clear();
    ExtractTrigrams(ToLower(text), out);
}

} // namespace

void TrigramIndex::Build(const IndexStore& store) {
    csrKeys_.clear();
    csrOffsets_.clear();
    csrPostings_.clear();
    view_ = {};
    entryCount_ = store.size();
    if (entryCount_ == 0) return;

    // 遍历 1：统计每个 trigram 的出现次数（计数 map 远小于 postings map）
    std::unordered_map<uint32_t, uint32_t> count;
    std::unordered_set<uint32_t> trigrams;
    for (size_t i = 0; i < entryCount_; ++i) {
        ExtractEntryTrigrams(store, i, trigrams);
        for (uint32_t tg : trigrams) ++count[tg];
    }

    // keys 升序 + offsets 前缀和 + postings 一次分配
    csrKeys_.reserve(count.size());
    for (const auto& [tg, c] : count) csrKeys_.push_back(tg);
    std::sort(csrKeys_.begin(), csrKeys_.end());
    csrOffsets_.resize(csrKeys_.size() + 1);
    csrOffsets_[0] = 0;
    for (size_t k = 0; k < csrKeys_.size(); ++k)
        csrOffsets_[k + 1] = csrOffsets_[k] + count[csrKeys_[k]];
    csrPostings_.resize(csrOffsets_.back());

    // 复用 count 作为各 posting list 的写入游标（起点 = 段偏移）
    for (size_t k = 0; k < csrKeys_.size(); ++k)
        count[csrKeys_[k]] = csrOffsets_[k];
    // 遍历 2：按 entry 序号递增填 postings（同 key 写入位置递增 → 各 list 升序）
    for (uint32_t i = 0; i < static_cast<uint32_t>(entryCount_); ++i) {
        ExtractEntryTrigrams(store, i, trigrams);
        for (uint32_t tg : trigrams) csrPostings_[count[tg]++] = i;
    }
}

std::vector<uint32_t> TrigramIndex::Query(std::wstring_view pattern) const {
    if (pattern.size() < 3) {
        // 短于 3 字符：返回所有条目（退化为全量扫描）
        std::vector<uint32_t> all(entryCount_);
        for (uint32_t i = 0; i < static_cast<uint32_t>(entryCount_); ++i)
            all[i] = i;
        return all;
    }

    std::wstring lower = ToLower(pattern);
    std::unordered_set<uint32_t> queryTrigrams;
    ExtractTrigrams(lower, queryTrigrams);
    if (queryTrigrams.empty()) return {};

    const CsrSegs segs = Segs();

    // 各查询 trigram 的 posting 段（二分定位；缺任一 → 无候选）
    std::vector<std::pair<const uint32_t*, const uint32_t*>> postingSegs;
    postingSegs.reserve(queryTrigrams.size());
    for (uint32_t tg : queryTrigrams) {
        const auto* k = std::lower_bound(segs.keys, segs.keys + segs.trigramCount, tg);
        if (k == segs.keys + segs.trigramCount || *k != tg) return {};
        const size_t ki = static_cast<size_t>(k - segs.keys);
        postingSegs.emplace_back(segs.postings + segs.offsets[ki],
                                 segs.postings + segs.offsets[ki + 1]);
    }

    // 从最短段开始逐步求交（升序双指针；候选集语义，不做精确验证）
    std::sort(postingSegs.begin(), postingSegs.end(),
              [](const auto& a, const auto& b) {
                  return (a.second - a.first) < (b.second - b.first);
              });
    std::vector<uint32_t> result(postingSegs[0].first, postingSegs[0].second);
    for (size_t s = 1; s < postingSegs.size() && !result.empty(); ++s) {
        const auto* p = postingSegs[s].first;
        const auto* q = postingSegs[s].second;
        std::vector<uint32_t> inter;
        size_t i = 0;
        size_t j = 0;
        while (i < result.size() && j < static_cast<size_t>(q - p)) {
            if (result[i] < p[j]) {
                ++i;
            } else if (result[i] > p[j]) {
                ++j;
            } else {
                inter.push_back(result[i]);
                ++i;
                ++j;
            }
        }
        result = std::move(inter);
    }
    return result;
}

size_t TrigramIndex::MemoryBytes() const {
    if (view_.keys != nullptr) return 0;  // 数据在文件映射中，不占进程堆
    return (csrKeys_.capacity() + csrOffsets_.capacity() + csrPostings_.capacity())
           * sizeof(uint32_t);
}

TrigramCsrView TrigramIndex::ExportCsrView() const {
    const CsrSegs s = Segs();
    TrigramCsrView v;
    v.keys         = s.keys;
    v.offsets      = s.offsets;
    v.postings     = s.postings;
    v.trigramCount = s.trigramCount;
    v.postingCount = s.trigramCount > 0 ? s.offsets[s.trigramCount] : 0;
    return v;
}

void TrigramIndex::ImportCsr(const uint32_t* keys, const uint32_t* offsets,
                             const uint32_t* postings, size_t trigramCount,
                             size_t entryCount) {
    view_ = {};
    csrKeys_.assign(keys, keys + trigramCount);
    csrOffsets_.assign(offsets, offsets + trigramCount + 1);
    csrPostings_.assign(postings, postings + offsets[trigramCount]);
    entryCount_ = entryCount;
}

void TrigramIndex::AttachView(const uint32_t* keys, const uint32_t* offsets,
                              const uint32_t* postings, size_t trigramCount,
                              size_t entryCount) {
    csrKeys_.clear();
    csrOffsets_.clear();
    csrPostings_.clear();
    view_ = {keys, offsets, postings, trigramCount};
    entryCount_ = entryCount;
}

} // namespace iris
