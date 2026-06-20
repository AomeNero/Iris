// Iris Engine —— 查询解析
// 设计依据: doc/detailed-design.md §7.2
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "provider/IProvider.h"

namespace iris {

struct ParsedQuery {
    std::vector<std::wstring> keywords;       // 小写关键词
    std::optional<ItemType>   filterType;      // nullopt = 搜全部 [已确认 E2]

    bool hasTypeFilter() const { return filterType.has_value(); }
    bool isEmpty() const { return keywords.empty() && !hasTypeFilter(); }
};

class QueryParser {
public:
    /// 解析用户输入（内部转小写）。# → APPLICATION，@ → BOOKMARK，无前缀 → 全部
    ParsedQuery Parse(std::wstring_view rawText);
};

} // namespace iris
