// Iris 数据模型层 —— Trigram 倒排索引
// 支持任意 3+ 字符子串搜索，O(K) 查询（K = 候选集大小）
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace iris {

class IndexStore;

class TrigramIndex {
public:
    /// 从 IndexStore 构建倒排索引（调用前须 Sort）
    void Build(const IndexStore& store);

    /// 查询：返回包含 pattern 的 entry 索引列表（已排序去重）
    std::vector<uint32_t> Query(std::wstring_view pattern) const;

    /// 条目数
    size_t EntryCount() const { return searchTexts_.size(); }

    /// 内存占用（字节）
    size_t MemoryBytes() const;

private:
    // trigram (3字符打包为 uint32_t) → 包含该 trigram 的 entry 索引列表
    std::unordered_map<uint32_t, std::vector<uint32_t>> index_;
    // 所有条目的 lowercase 搜索文本
    std::vector<std::wstring> searchTexts_;

    static uint32_t PackTrigram(wchar_t a, wchar_t b, wchar_t c);
    static std::wstring ToLower(std::wstring_view s);
    static void ExtractTrigrams(std::wstring_view text, std::unordered_set<uint32_t>& out);
};

} // namespace iris
