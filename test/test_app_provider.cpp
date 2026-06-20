// AppProvider 单元测试：聚焦按名去重逻辑（.lnk 解析依赖真实 COM，留端到端验证）
#include <gtest/gtest.h>

#include <vector>

#include "provider/AppProvider.h"

using iris::AppProvider;

TEST(AppProviderTest, DeduplicateByNameKeepsFirstCaseInsensitive) {
    std::vector<AppProvider::Entry> es = {
        {L"notepad", L"C:\\Windows\\a.exe"},
        {L"Notepad", L"C:\\Tools\\b.exe"},  // 与 notepad 同名(CI)，应丢弃
        {L"calc",    L"C:\\Windows\\c.exe"},
    };
    AppProvider::DeduplicateByName(es);

    ASSERT_EQ(es.size(), 2u);
    EXPECT_EQ(es[0].title,      L"notepad");
    EXPECT_EQ(es[0].targetPath, L"C:\\Windows\\a.exe");  // 保留先出现（开始菜单优先）
    EXPECT_EQ(es[1].title,      L"calc");
}

TEST(AppProviderTest, DeduplicatePreservesOrder) {
    std::vector<AppProvider::Entry> es = {
        {L"Zebra",  L"z"},
        {L"Apple",  L"a"},
        {L"apple",  L"a2"},   // 重复，丢弃
        {L"Mango",  L"m"},
    };
    AppProvider::DeduplicateByName(es);

    ASSERT_EQ(es.size(), 3u);
    EXPECT_EQ(es[0].title, L"Zebra");
    EXPECT_EQ(es[1].title, L"Apple");
    EXPECT_EQ(es[2].title, L"Mango");
}

TEST(AppProviderTest, EmptyInputStaysEmpty) {
    std::vector<AppProvider::Entry> es;
    AppProvider::DeduplicateByName(es);
    EXPECT_TRUE(es.empty());
}
