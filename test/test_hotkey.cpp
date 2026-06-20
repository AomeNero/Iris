// 测试：HotkeySpec 字符串 ↔ (modifiers, vkCode)
#include <gtest/gtest.h>

#include "core/HotkeySpec.h"

using namespace iris::HotkeySpec;

// MOD_ALT=0x1 MOD_CONTROL=0x2 MOD_SHIFT=0x4 MOD_WIN=0x8
TEST(HotkeyParseTest, ParsesAltSpace) {
    unsigned mods = 0, vk = 0;
    ASSERT_TRUE(Parse("Alt+Space", mods, vk));
    EXPECT_EQ(mods, 0x0001u);
    EXPECT_EQ(vk, 0x20u);  // VK_SPACE
}

TEST(HotkeyParseTest, ParsesMultipleModifiers) {
    unsigned mods = 0, vk = 0;
    ASSERT_TRUE(Parse("Ctrl+Alt+P", mods, vk));
    EXPECT_EQ(mods, 0x0001u | 0x0002u);
    EXPECT_EQ(vk, 0x50u);  // 'P'
}

TEST(HotkeyParseTest, ParsesWinKey) {
    unsigned mods = 0, vk = 0;
    ASSERT_TRUE(Parse("Win+Space", mods, vk));
    EXPECT_EQ(mods, 0x0008u);
    EXPECT_EQ(vk, 0x20u);
}

TEST(HotkeyParseTest, ParsesFunctionKey) {
    unsigned mods = 0, vk = 0;
    ASSERT_TRUE(Parse("Alt+F5", mods, vk));
    EXPECT_EQ(mods, 0x0001u);
    EXPECT_EQ(vk, 0x74u);  // VK_F5
}

TEST(HotkeyParseTest, CaseAndSpaceInsensitive) {
    unsigned mods = 0, vk = 0;
    ASSERT_TRUE(Parse("  alt + space ", mods, vk));
    EXPECT_EQ(mods, 0x0001u);
    EXPECT_EQ(vk, 0x20u);
}

TEST(HotkeyParseTest, RejectsInvalid) {
    unsigned mods = 99, vk = 99;
    EXPECT_FALSE(Parse("Foo+Bar", mods, vk));
    EXPECT_FALSE(Parse("Alt", mods, vk));     // 无主键
    EXPECT_FALSE(Parse("Space", mods, vk));   // 无修饰键
    EXPECT_FALSE(Parse("", mods, vk));
    // 失败时不修改输出参数
    EXPECT_EQ(mods, 99u);
    EXPECT_EQ(vk, 99u);
}

TEST(HotkeyStringTest, ToStringAltSpace) {
    EXPECT_EQ(ToString(0x0001u, 0x20u), "Alt+Space");
}

TEST(HotkeyStringTest, ToStringCtrlAltP) {
    EXPECT_EQ(ToString(0x0001u | 0x0002u, 0x50u), "Ctrl+Alt+P");
}
