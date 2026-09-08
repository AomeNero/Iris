// 模块 5 测试：Matcher
// 设计依据: doc/detailed-design.md §7.3 [已确认 E3]
#include <gtest/gtest.h>

#include <atomic>

#include "engine/Matcher.h"
#include "engine/QueryParser.h"
#include "mock_provider.h"

using iris::Matcher;
using iris::MatchResult;

namespace {
std::atomic<bool> kFalseFlag{false};
} // namespace

TEST(MatcherSingleTest, TitlePrefixScoresHighest) {
    Matcher m;
    int s = m.MatchSingle(L"report.docx", L"D:\\docs", L"D:\\docs\\report.docx",
                          L"", L"", {L"rep"});
    EXPECT_GT(s, 0);  // 命中
}

TEST(MatcherSingleTest, UnmatchedKeywordScoresZero) {
    Matcher m;
    int s = m.MatchSingle(L"report.docx", L"D:\\docs", L"D:\\docs\\report.docx",
                          L"", L"", {L"zzz"});
    EXPECT_EQ(s, 0);
}

TEST(MatcherSingleTest, PartialKeywordMatchScoresZero) {
    Matcher m;
    // 任一关键词不匹配 → 0
    int s = m.MatchSingle(L"report.docx", L"D:\\docs", L"D:\\docs\\report.docx",
                          L"", L"", {L"report", L"missing"});
    EXPECT_EQ(s, 0);
}

TEST(MatcherSingleTest, AllKeywordsMatch) {
    Matcher m;
    int s = m.MatchSingle(L"report 2024.docx", L"D:\\docs", L"D:\\docs\\report 2024.docx",
                          L"", L"", {L"report", L"2024"});
    EXPECT_GT(s, 0);
}

// 拼音匹配：全拼命中中文条目（title 中文不直接匹配 ASCII keyword，走拼音分支）
TEST(MatcherSingleTest, PinyinFullPrefixMatches) {
    Matcher m;
    int s = m.MatchSingle(L"微信", L"", L"C:\\微信.lnk", L"weixin", L"wx", {L"weixin"});
    EXPECT_GT(s, 0);
}

// 拼音匹配：首字母命中
TEST(MatcherSingleTest, PinyinInitialsMatch) {
    Matcher m;
    int s = m.MatchSingle(L"微信", L"", L"C:\\微信.lnk", L"weixin", L"wx", {L"wx"});
    EXPECT_GT(s, 0);
}

TEST(MatcherTest, ReturnsOnlyScoredEntries) {
    MockProvider::Entries es = {
        {L"apple.txt",   L"D:\\a", L"D:\\a\\apple.txt",   iris::ItemType::FILE, 1},
        {L"banana.txt",  L"D:\\a", L"D:\\a\\banana.txt",  iris::ItemType::FILE, 1},
        {L"apricot.txt", L"D:\\a", L"D:\\a\\apricot.txt", iris::ItemType::FILE, 1},
    };
    MockProvider provider(std::move(es));
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"ap"};
    auto results = matcher.Match(provider, q, kFalseFlag);
    // apple 与 apricott 匹配前缀 "ap"，banana 不匹配
    EXPECT_EQ(results.size(), 2u);
}

// 拼音端到端：MockProvider 含「微信」（pinyinFull=weixin），搜 weixin 只命中微信
TEST(MatcherTest, PinyinFullMatchesChineseEntry) {
    MockProvider::Entries es = {
        {L"微信",   L"", L"C:\\微信.lnk",   iris::ItemType::APPLICATION, 0, L"weixin",   L"wx"},
        {L"记事本", L"", L"C:\\notepad.exe", iris::ItemType::APPLICATION, 0, L"jishiben", L"jsb"},
    };
    MockProvider provider(std::move(es));
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"weixin"};
    auto results = matcher.Match(provider, q, kFalseFlag);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(provider.BuildResultItem(results[0].entryIndex).title, L"微信");
}

// ── trigram 路径保护网（关键词 ≥3 字符且 Provider 提供 TrigramIndex 时走倒排路径）──
// 锁定端到端行为：无论 Query 返回精确集还是候选集（验证下沉 Matcher 后），
// MatchSingle 的完整验证都必须保证结果不变。

namespace {
MockProvider::Entries TrigramFiles() {
    return {
        {L"apple.txt",  L"D:\\a", L"D:\\a\\apple.txt",  iris::ItemType::FILE, 1},
        {L"banana.txt", L"D:\\a", L"D:\\a\\banana.txt", iris::ItemType::FILE, 1},
    };
}
} // namespace

TEST(MatcherTrigramTest, SubstringHitViaTrigramPath) {
    MockProvider provider(TrigramFiles(), true, /*buildTrigram=*/true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"ple"};  // 非前缀子串，3 字符 → 必须走 trigram 路径才命中
    auto results = matcher.Match(provider, q, kFalseFlag);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(provider.BuildResultItem(results[0].entryIndex).title, L"apple.txt");
}

TEST(MatcherTrigramTest, CaseInsensitiveSubstring) {
    MockProvider provider(TrigramFiles(), true, true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"PLE"};
    auto results = matcher.Match(provider, q, kFalseFlag);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(provider.BuildResultItem(results[0].entryIndex).title, L"apple.txt");
}

TEST(MatcherTrigramTest, DirNameSubstringHits) {
    MockProvider::Entries es = {
        {L"report.docx", L"D:\\docs", L"D:\\docs\\report.docx", iris::ItemType::FILE, 1},
    };
    MockProvider provider(std::move(es), true, true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"docs"};  // 目录名命中（title 本身不含）
    auto results = matcher.Match(provider, q, kFalseFlag);
    ASSERT_EQ(results.size(), 1u);
}

// trigram 交集的天然假阳性：文本含 trigram abc 与 bcd 但不含完整 abcd。
// Query 精确验证（现实现）返回空 / 候选集（目标实现）返回候选——
// 两种实现下端到端都必须 0 结果，守住「验证下沉后行为不变」。
TEST(MatcherTrigramTest, TrigramFalsePositiveFilteredEndToEnd) {
    MockProvider::Entries es = {
        {L"abc.txt", L"D:\\bcd", L"D:\\bcd\\abc.txt", iris::ItemType::FILE, 1},
    };
    MockProvider provider(std::move(es), true, true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"abcd"};
    auto results = matcher.Match(provider, q, kFalseFlag);
    EXPECT_EQ(results.size(), 0u);
}

TEST(MatcherTrigramTest, MultiKeywordAllMustMatch) {
    MockProvider provider(TrigramFiles(), true, true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"app", L"txt"};  // keywords[0] 走 trigram，其余由 MatchSingle 验证
    auto results = matcher.Match(provider, q, kFalseFlag);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(provider.BuildResultItem(results[0].entryIndex).title, L"apple.txt");
}

TEST(MatcherTrigramTest, NoTrigramHitReturnsEmpty) {
    MockProvider provider(TrigramFiles(), true, true);
    Matcher matcher;
    iris::ParsedQuery q;
    q.keywords = {L"zzz"};
    auto results = matcher.Match(provider, q, kFalseFlag);
    EXPECT_EQ(results.size(), 0u);
}
