// Iris 数据模型层 —— Trigram 倒排索引实现
#include "provider/TrigramIndex.h"
#include "provider/IndexStore.h"

#include <algorithm>
#include <cwchar>

namespace iris {

// 将 3 个 wchar_t 打包为 uint32_t（低 16+8+8 位）
uint32_t TrigramIndex::PackTrigram(wchar_t a, wchar_t b, wchar_t c) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(a)) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(c)));
}

std::wstring TrigramIndex::ToLower(std::wstring_view s) {
    std::wstring result(s);
    for (auto& ch : result) {
        ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(ch)));
    }
    return result;
}

void TrigramIndex::ExtractTrigrams(std::wstring_view text, std::unordered_set<uint32_t>& out) {
    if (text.size() < 3) return;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        out.insert(PackTrigram(text[i], text[i + 1], text[i + 2]));
    }
}

void TrigramIndex::Build(const IndexStore& store) {
    index_.clear();
    searchTexts_.clear();

    const size_t n = store.size();
    searchTexts_.reserve(n);

    // 第一遍：生成所有条目的 lowercase 搜索文本，提取 trigram
    for (size_t i = 0; i < n; ++i) {
        const auto& entry = store[i];
        // 搜索文本 = lowercase(name + " " + dir)
        std::wstring text;
        auto name = store.NameAt(entry);
        auto dir = store.DirOf(entry);
        text.reserve(name.size() + 1 + dir.size());
        text.assign(name);
        text += L' ';
        text.append(dir);
        std::wstring lower = ToLower(text);
        searchTexts_.push_back(std::move(lower));
    }

    // 第二遍：构建倒排索引
    std::unordered_set<uint32_t> trigrams;
    for (size_t i = 0; i < n; ++i) {
        trigrams.clear();
        ExtractTrigrams(searchTexts_[i], trigrams);
        for (uint32_t tg : trigrams) {
            index_[tg].push_back(static_cast<uint32_t>(i));
        }
    }
}

std::vector<uint32_t> TrigramIndex::Query(std::wstring_view pattern) const {
    if (pattern.size() < 3) {
        // 短于 3 字符：返回所有条目（退化为全量扫描）
        std::vector<uint32_t> all(searchTexts_.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(searchTexts_.size()); ++i)
            all[i] = i;
        return all;
    }

    // 提取查询的 trigram
    std::wstring lower = ToLower(pattern);
    std::unordered_set<uint32_t> queryTrigrams;
    ExtractTrigrams(lower, queryTrigrams);

    if (queryTrigrams.empty()) return {};

    // 找到包含所有 trigram 的交集
    // 策略：从最小的 posting list 开始，逐步过滤
    std::vector<uint32_t> result;
    bool first = true;

    for (uint32_t tg : queryTrigrams) {
        auto it = index_.find(tg);
        if (it == index_.end()) {
            // 某个 trigram 不存在 → 无结果
            return {};
        }
        if (first) {
            result = it->second;
            first = false;
        } else {
            // 交集：保留同时在 result 和 posting list 中的条目
            std::vector<uint32_t> intersection;
            const auto& posting = it->second;
            size_t i = 0, j = 0;
            while (i < result.size() && j < posting.size()) {
                if (result[i] < posting[j]) {
                    ++i;
                } else if (result[i] > posting[j]) {
                    ++j;
                } else {
                    intersection.push_back(result[i]);
                    ++i;
                    ++j;
                }
            }
            result = std::move(intersection);
        }
        if (result.empty()) return {};
    }

    // 精确过滤：trigram 是子串的必要条件，但不是充分条件
    // 需要验证实际子串匹配
    std::vector<uint32_t> verified;
    verified.reserve(result.size());
    for (uint32_t idx : result) {
        if (searchTexts_[idx].find(lower) != std::wstring::npos) {
            verified.push_back(idx);
        }
    }
    return verified;
}

size_t TrigramIndex::MemoryBytes() const {
    size_t bytes = 0;
    // index_ 的 trigram 键 + posting list
    for (const auto& [tg, list] : index_) {
        bytes += sizeof(uint32_t) + list.capacity() * sizeof(uint32_t);
    }
    // searchTexts_
    for (const auto& s : searchTexts_) {
        bytes += s.capacity() * sizeof(wchar_t);
    }
    return bytes;
}

} // namespace iris
