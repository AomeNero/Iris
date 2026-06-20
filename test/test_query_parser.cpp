// 模块 5 测试：QueryParser
// 设计依据: doc/detailed-design.md §7.2 [已确认 E2]
#include <gtest/gtest.h>

#include "engine/QueryParser.h"

using iris::ItemType;
using iris::QueryParser;
using iris::ParsedQuery;

namespace {
ParsedQuery Parse(const std::wstring& s) { return QueryParser{}.Parse(s); }
} // namespace

TEST(QueryParserTest, SingleKeyword) {
    auto q = Parse(L"报告");
    ASSERT_EQ(q.keywords.size(), 1u);
    EXPECT_EQ(q.keywords[0], L"报告");
    EXPECT_FALSE(q.hasTypeFilter());
}

TEST(QueryParserTest, MultipleKeywordsLowercased) {
    auto q = Parse(L"Report 2024");
    ASSERT_EQ(q.keywords.size(), 2u);
    EXPECT_EQ(q.keywords[0], L"report");
    EXPECT_EQ(q.keywords[1], L"2024");
}

TEST(QueryParserTest, HashPrefixMeansApplication) {
    auto q = Parse(L"#Notepad");
    EXPECT_TRUE(q.hasTypeFilter());
    EXPECT_EQ(q.filterType, ItemType::APPLICATION);
    ASSERT_EQ(q.keywords.size(), 1u);
    EXPECT_EQ(q.keywords[0], L"notepad");
}

TEST(QueryParserTest, AtPrefixMeansBookmark) {
    auto q = Parse(L"@github");
    EXPECT_EQ(q.filterType, ItemType::BOOKMARK);
    ASSERT_EQ(q.keywords.size(), 1u);
}

TEST(QueryParserTest, OnlyPrefixNoKeywordsShowsAllOfType) {
    auto q = Parse(L"#");
    EXPECT_TRUE(q.hasTypeFilter());
    EXPECT_TRUE(q.keywords.empty());
    EXPECT_FALSE(q.isEmpty());  // 仅前缀也非空查询 → 展示该类型全部
}

TEST(QueryParserTest, EmptyInputIsEmpty) {
    auto q = Parse(L"");
    EXPECT_TRUE(q.isEmpty());
    EXPECT_TRUE(q.keywords.empty());
    EXPECT_FALSE(q.hasTypeFilter());

    auto q2 = Parse(L"   ");
    EXPECT_TRUE(q2.isEmpty());
}
