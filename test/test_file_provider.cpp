// 模块 4 测试：FileProvider 排除规则
// 设计依据: doc/code-prompt.md §六 Module4, doc/detailed-design.md §6.1.6 [B2]
//
// 注：USN 记录解析与 CoW 在真实卷上集成验证（需 NTFS 卷 + 权限），
//     此处单测覆盖纯函数 ShouldExclude 的排除规则。
#include <gtest/gtest.h>

#include <windows.h>

#include "provider/FileProvider.h"

using iris::FileProvider;

TEST(FileProviderExcludeTest, ExcludesHiddenAndSystem) {
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_HIDDEN, L"foo.txt"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_SYSTEM, L"foo.txt"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY, L"foo"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE, L"bar"));
}

TEST(FileProviderExcludeTest, ExcludesByExtension) {
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"setup.dll"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"trace.log"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"driver.sys"));
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"cache.tmp"));
    // 扩展名转小写后匹配
    EXPECT_TRUE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"UPPER.DLL"));
}

TEST(FileProviderExcludeTest, KeepsNormalFiles) {
    EXPECT_FALSE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"report.docx"));
    EXPECT_FALSE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"main.cpp"));
    EXPECT_FALSE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"notes.txt"));  // .txt 不在排除表
    EXPECT_FALSE(FileProvider::ShouldExclude(FILE_ATTRIBUTE_NORMAL, L"noext"));
}
