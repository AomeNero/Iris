// Iris Engine —— Ranker 实现
#include "engine/Ranker.h"

#include <algorithm>
#include <cmath>

namespace iris {

int Ranker::TypeScore(ItemType type) {
    switch (type) {
        case ItemType::APPLICATION: return 100;
        case ItemType::FILE:        return 80;
        case ItemType::BOOKMARK:    return 60;
    }
    return 0;
}

int Ranker::PathDepthScore(uint8_t depth) {
    int s = 100 - static_cast<int>(depth) * 10;
    return s < 0 ? 0 : s;
}

int Ranker::HistoryScore(int openCount) {
    if (openCount <= 0) return 0;
    double s = std::log2(static_cast<double>(openCount) + 1.0) * 25.0;
    return s > 100.0 ? 100 : static_cast<int>(s);
}

namespace {
struct Scored {
    MatchResult m;
    int         finalScore;
};
} // namespace

std::vector<ResultItem> Ranker::Rank(std::vector<MatchResult>& matches,
                                     HistoryStore* history, int maxResults) {
    std::vector<Scored> scored;
    scored.reserve(matches.size());

    for (auto& m : matches) {
        if (!m.provider) continue;
        const ItemType type = m.provider->GetType(m.entryIndex);
        const uint8_t depth = m.provider->GetPathDepth(m.entryIndex);

        int historyScore = 0;
        if (history) {
            // 用完整路径查询打开次数
            const ResultItem ri = m.provider->BuildResultItem(m.entryIndex);
            historyScore = HistoryScore(history->GetOpenCount(ri.path));
        }

        // finalScore = 相关度(position+coverage, 合并到 rawScore)×0.35
        //            + 类型×0.20 + 路径深度×0.05 + 历史×0.40
        // 权重和 = 1.0（详见设计 §7.4；position/coverage 已并入 rawScore）
        const float relevanceWeight = kWeightPosition + kWeightCoverage;  // 0.35
        const int finalScore = static_cast<int>(
            m.rawScore            * relevanceWeight +
            TypeScore(type)       * kWeightType +
            PathDepthScore(depth) * kWeightPathDepth +
            historyScore          * kWeightHistory);

        scored.push_back({m, finalScore});
    }

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.finalScore > b.finalScore; });

    std::vector<ResultItem> out;
    const int n = static_cast<int>(scored.size()) < maxResults
                      ? static_cast<int>(scored.size())
                      : maxResults;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const MatchResult& m = scored[i].m;
        ResultItem item = m.provider->BuildResultItem(m.entryIndex);
        item.score = scored[i].finalScore;
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace iris
