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

std::vector<ResultItem> Ranker::Rank(std::vector<MatchResult>& matches,
                                     HistoryStore* history, int maxResults) {
    if (matches.empty()) return {};
    const float relevanceWeight = kWeightPosition + kWeightCoverage;  // 0.35

    // 阶段1：粗排（全量、廉价——不查历史、不拼路径）。
    // 百万级 matches 下避免全量 GetOpenCount（每次 SQL prepare+lock）+ BuildResultItem（路径拼接）。
    struct Coarse { const MatchResult* m; int score; };
    std::vector<Coarse> coarse;
    coarse.reserve(matches.size());
    for (auto& m : matches) {
        if (!m.provider) continue;
        const ItemType type = m.provider->GetType(m.entryIndex);
        const uint8_t depth = m.provider->GetPathDepth(m.entryIndex);
        coarse.push_back({&m, static_cast<int>(
            m.rawScore            * relevanceWeight +
            TypeScore(type)       * kWeightType +
            PathDepthScore(depth) * kWeightPathDepth)});
    }
    if (coarse.empty()) return {};

    // 粗排取前 K（O(n) nth_element）。历史 0.40 权重在阶段2 补；K 足够覆盖高相关度项。
    constexpr int kTopK = 256;
    const int k = std::min(static_cast<int>(coarse.size()), kTopK);
    std::nth_element(coarse.begin(), coarse.begin() + k, coarse.end(),
                     [](const Coarse& a, const Coarse& b) { return a.score > b.score; });

    // 阶段2：精排前 K（查历史 + 完整分数 = 相关度+类型+路径深度+历史）
    struct Fine { const MatchResult* m; int score; };
    std::vector<Fine> fine;
    fine.reserve(k);
    for (int i = 0; i < k; ++i) {
        const MatchResult& m = *coarse[i].m;
        const ItemType type = m.provider->GetType(m.entryIndex);
        const uint8_t depth = m.provider->GetPathDepth(m.entryIndex);
        int historyScore = 0;
        if (history) {
            const ResultItem ri = m.provider->BuildResultItem(m.entryIndex);
            historyScore = HistoryScore(history->GetOpenCount(ri.path));
        }
        fine.push_back({&m, static_cast<int>(
            m.rawScore            * relevanceWeight +
            TypeScore(type)       * kWeightType +
            PathDepthScore(depth) * kWeightPathDepth +
            historyScore          * kWeightHistory)});
    }
    std::sort(fine.begin(), fine.end(),
              [](const Fine& a, const Fine& b) { return a.score > b.score; });

    const int n = std::min(static_cast<int>(fine.size()), maxResults);
    std::vector<ResultItem> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const MatchResult& m = *fine[i].m;
        ResultItem item = m.provider->BuildResultItem(m.entryIndex);
        item.score = fine[i].score;
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace iris
