// Iris 数据模型层 —— 可搜索数据源扩展接口
// 设计依据: doc/detailed-design.md §4.2.2
//
// 搜索时 Matcher/Ranker 通过此接口直接查询 Provider，避免中间
// std::vector<ResultItem> 的转换开销。FileProvider 走二分查找快速路径
// (1M+ 条目)；Bookmark/App 也实现此接口（条目少，线性扫描）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "provider/IProvider.h"
#include "provider/TrigramIndex.h"  // SearchTrigram 默认实现需完整类型

namespace iris {

class ISearchableProvider : public IProvider {
public:
    ~ISearchableProvider() override = default;

    /// 二分查找文件名前缀，返回起始下标。
    /// FileProvider: O(log n) 二分查找；Bookmark/App: 返回 0（线性扫描）
    virtual size_t FindFirstPrefix(std::wstring_view prefix) const = 0;

    /// 从紧凑条目构建 ResultItem（仅对命中条目调用，最多数十次）
    virtual ResultItem BuildResultItem(size_t index) const = 0;

    /// 获取搜索文本（按需拼接 title + path，不预存）
    virtual std::wstring GetSearchText(size_t index) const = 0;

    /// 拼音（无音调全拼/首字母，拼音匹配用）。默认空——FileProvider 用默认，App/Bookmark override。
    virtual std::wstring GetPinyinFull(size_t /*index*/) const { return {}; }
    virtual std::wstring GetPinyinInitials(size_t /*index*/) const { return {}; }

    /// 元数据访问（Ranker 排序时按需调用）
    virtual ItemType GetType(size_t index) const = 0;
    virtual uint8_t  GetPathDepth(size_t index) const = 0;

    /// 获取完整路径（精排 GetOpenCount 用，比 BuildResultItem 更轻量）
    virtual std::wstring GetPath(size_t index) const = 0;

    /// 获取 Trigram 倒排索引（子串搜索用；默认返回 nullptr 表示不支持）
    virtual const TrigramIndex* GetTrigramIndex() const { return nullptr; }

    /// Trigram 子串搜索：返回候选 entry 索引（必要条件集）。
    /// 支持实时增量的实现覆写此方法以合并 delta 候选（Matcher 经此搜索，
    /// 门控仍走 GetTrigramIndex 判空）。默认：对 GetTrigramIndex 直接 Query。
    virtual std::vector<uint32_t> SearchTrigram(std::wstring_view pattern) const {
        const auto* t = GetTrigramIndex();
        if (!t || t->EntryCount() == 0) return {};
        return t->Query(pattern);
    }
};

} // namespace iris
