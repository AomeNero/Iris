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
};

} // namespace iris
