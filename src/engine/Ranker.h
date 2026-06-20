// Iris Engine —— 排序器
// 设计依据: doc/detailed-design.md §7.4
#pragma once

#include <vector>

#include "core/HistoryStore.h"
#include "engine/Matcher.h"
#include "provider/IProvider.h"

namespace iris {

class Ranker {
public:
    /// 对匹配结果跨 Provider 排序并截断为 Top-N
    /// 注：results 会原地排序；matches 的 provider 字段用于按需查询元数据。
    std::vector<ResultItem> Rank(std::vector<MatchResult>& matches,
                                 HistoryStore* history,
                                 int maxResults);

    // 权重（公开便于测试/调参）
    static constexpr float kWeightPosition   = 0.30f;
    static constexpr float kWeightCoverage   = 0.15f;
    static constexpr float kWeightType       = 0.05f;
    static constexpr float kWeightPathDepth  = 0.10f;
    static constexpr float kWeightHistory    = 0.40f;

    static int TypeScore(ItemType type);
    static int PathDepthScore(uint8_t depth);
    static int HistoryScore(int openCount);
};

} // namespace iris
