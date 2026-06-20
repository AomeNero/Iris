// 模块 5 测试：Ranker
// 设计依据: doc/detailed-design.md §7.4
#include <gtest/gtest.h>

#include "core/HistoryStore.h"
#include "engine/Matcher.h"
#include "engine/Ranker.h"
#include "mock_provider.h"

using iris::MatchResult;
using iris::Ranker;
using iris::ResultItem;

namespace {
// 构造一个命中（仅记录 provider + index；分数由 rawScore 给）
MatchResult Mk(iris::ISearchableProvider* p, std::size_t i, int raw) {
    MatchResult m;
    m.provider = p; m.entryIndex = i; m.rawScore = raw;
    return m;
}
} // namespace

TEST(RankerTest, ApplicationRanksAboveFileAboveBookmark) {
    // 同样 rawScore、同深度、无历史 → 类型决定顺序
    MockProvider::Entries es = {
        {L"bookmark", L"u", L"url", iris::ItemType::BOOKMARK, 0},
        {L"file",     L"d", L"d\\file", iris::ItemType::FILE, 0},
        {L"app",      L"a", L"a\\app", iris::ItemType::APPLICATION, 0},
    };
    MockProvider provider(std::move(es));
    std::vector<MatchResult> matches = {
        Mk(&provider, 2, 50),  // bookmark
        Mk(&provider, 1, 50),  // file
        Mk(&provider, 0, 50),  // app
    };
    Ranker ranker;
    auto out = ranker.Rank(matches, nullptr, 9);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].title, L"app");
    EXPECT_EQ(out[1].title, L"file");
    EXPECT_EQ(out[2].title, L"bookmark");
}

TEST(RankerTest, TruncatesToMaxResults) {
    MockProvider::Entries es = {
        {L"a", L"d", L"d\\a", iris::ItemType::FILE, 0},
        {L"b", L"d", L"d\\b", iris::ItemType::FILE, 0},
        {L"c", L"d", L"d\\c", iris::ItemType::FILE, 0},
    };
    MockProvider provider(std::move(es));
    std::vector<MatchResult> matches = {
        Mk(&provider, 0, 10), Mk(&provider, 1, 20), Mk(&provider, 2, 30),
    };
    Ranker ranker;
    auto out = ranker.Rank(matches, nullptr, 2);
    EXPECT_EQ(out.size(), 2u);
    // 分数高的在前
    EXPECT_EQ(out[0].title, L"c");
    EXPECT_EQ(out[1].title, L"b");
}

TEST(RankerTest, HistoryBoostsRanking) {
    MockProvider::Entries es = {
        {L"a", L"d", L"p\\a", iris::ItemType::FILE, 0},
        {L"b", L"d", L"p\\b", iris::ItemType::FILE, 0},
    };
    MockProvider provider(std::move(es));

    // 给 "b" 记录打开历史
    iris::HistoryStore hist(L":memory:");
    {
        ResultItem b; b.title = L"b"; b.path = L"p\\b"; b.type = iris::ItemType::FILE;
        hist.RecordOpen(b);
        hist.RecordOpen(b);  // open_count=2
    }

    std::vector<MatchResult> matches = {
        Mk(&provider, 0, 50),  // a，无历史
        Mk(&provider, 1, 50),  // b，有历史 → 应排前
    };
    Ranker ranker;
    auto out = ranker.Rank(matches, &hist, 9);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].title, L"b");  // 历史加权后 b 胜出
}
