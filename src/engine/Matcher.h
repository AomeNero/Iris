// Iris Engine —— 匹配器
// 设计依据: doc/detailed-design.md §7.3
#pragma once

#include <atomic>
#include <string_view>
#include <vector>

#include "engine/QueryParser.h"
#include "provider/ISearchableProvider.h"

namespace iris {

struct MatchResult {
    ISearchableProvider* provider = nullptr;  // 命中来源（跨 Provider 排序用）
    std::size_t          entryIndex = 0;
    int                  rawScore = 0;
    int                  positionScore = 0;
    int                  coverageScore = 0;
};

class Matcher {
public:
    /// 对一个 Provider 的条目执行匹配。返回 score>0 的结果。
    std::vector<MatchResult> Match(ISearchableProvider& provider,
                                   const ParsedQuery& query,
                                   const std::atomic<bool>& cancelled);

    /// 对单条目打分（公开便于单测）
    int MatchSingle(std::wstring_view title,
                    std::wstring_view subtitle,
                    std::wstring_view path,
                    const std::vector<std::wstring>& keywords);
};

} // namespace iris
