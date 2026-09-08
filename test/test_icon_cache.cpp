// LruCache + IconCache 测试
// IconCache 契约：key=扩展名归一化（大小写无关，无扩展名共享兜底），
//                LRU 容量驱逐，loader 注入（同 key 只加载一次）。
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QPixmap>

#include "core/LruCache.h"
#include "ui/IconCache.h"

using iris::IconCache;
using iris::LruCache;

// ── LruCache：纯逻辑 ──

TEST(LruCacheTest, PutGetBasic) {
    LruCache<int, int> c(2);
    c.Put(1, 10);
    c.Put(2, 20);
    ASSERT_TRUE(c.Get(1).has_value());
    EXPECT_EQ(*c.Get(1), 10);
    EXPECT_EQ(*c.Get(2), 20);
    EXPECT_EQ(c.Size(), 2u);
}

TEST(LruCacheTest, GetMissingReturnsNullopt) {
    LruCache<int, int> c(2);
    EXPECT_FALSE(c.Get(42).has_value());
}

TEST(LruCacheTest, EvictsLeastRecentlyUsed) {
    LruCache<int, int> c(2);
    c.Put(1, 10);
    c.Put(2, 20);
    c.Get(1);        // 触碰 1 → 2 成为最久未用
    c.Put(3, 30);    // 驱逐 2
    EXPECT_FALSE(c.Contains(2));
    EXPECT_TRUE(c.Contains(1));
    EXPECT_TRUE(c.Contains(3));
}

TEST(LruCacheTest, PutExistingKeyRefreshesAndUpdateValue) {
    LruCache<int, int> c(2);
    c.Put(1, 10);
    c.Put(2, 20);
    c.Put(1, 11);    // 触碰 1 并更新值
    c.Put(3, 30);    // 驱逐 2（而非 1）
    EXPECT_TRUE(c.Contains(1));
    EXPECT_EQ(*c.Get(1), 11);
}

TEST(LruCacheTest, ZeroCapacityHoldsNothing) {
    LruCache<int, int> c(0);
    c.Put(1, 10);
    EXPECT_EQ(c.Size(), 0u);
    EXPECT_FALSE(c.Contains(1));
}

TEST(LruCacheTest, ContainsDoesNotRefreshRecency) {
    LruCache<int, int> c(2);
    c.Put(1, 10);
    c.Put(2, 20);
    c.Contains(1);   // 仅观测，不触碰
    c.Put(3, 30);    // 仍驱逐 1（最久未「用」）
    EXPECT_FALSE(c.Contains(1));
    EXPECT_TRUE(c.Contains(2));
}

// ── IconCache ──

namespace {
IconCache::Loader CountingLoader(int& calls) {
    return [&calls](const std::wstring& /*path*/) {
        ++calls;
        return QPixmap();
    };
}
} // namespace

TEST(IconCacheTest, SameExtensionLoadsOnce) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\a\\foo.png");
    c.Get(L"D:\\b\\bar.png");
    c.Get(L"D:\\c.PNG");   // 扩展名大小写归一
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(c.Size(), 1u);
}

TEST(IconCacheTest, DifferentExtensionLoadsSeparately) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\a\\x.png");
    c.Get(L"D:\\a\\y.jpg");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(c.Size(), 2u);
}

TEST(IconCacheTest, NoExtensionSharesFallbackKey) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\x\\Makefile");
    c.Get(L"D:\\y\\LICENSE");
    EXPECT_EQ(calls, 1);   // 无扩展名文件共享兜底图标
}

// 点出现在目录部分不算扩展名：D:\my.dir\file 与 C:\other\file2 同为无扩展名
TEST(IconCacheTest, DotInDirectoryIsNotExtension) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\my.dir\\file");
    c.Get(L"C:\\other\\file2");
    EXPECT_EQ(calls, 1);
}

TEST(IconCacheTest, EvictedEntryReloads) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 1);
    c.Get(L"a.png");   // calls=1
    c.Get(L"b.jpg");   // 驱逐 png，calls=2
    c.Get(L"c.png");   // png 已被驱逐 → 重载，calls=3
    EXPECT_EQ(calls, 3);
}

TEST(IconCacheTest, EmptyPathReturnsNullWithoutLoading) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 4);
    EXPECT_TRUE(c.Get(L"").isNull());
    EXPECT_EQ(calls, 0);
}

TEST(IconCacheTest, FailedLoadIsCachedToo) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"a.xyz");
    c.Get(L"a.xyz");
    EXPECT_EQ(calls, 1);   // 空结果也缓存，避免反复加载失败
}

// ── 混合 key：图标类扩展名（exe/lnk/ico/msi/scr）按完整路径缓存（各自真实图标）──

TEST(IconCacheTest, IconBearingExtensionCachesPerPath) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\a\\wechat.exe");
    c.Get(L"D:\\b\\vs.exe");
    c.Get(L"D:\\c\\app.lnk");
    EXPECT_EQ(calls, 3);   // 不同 exe/lnk 各自提取，不共享
    EXPECT_EQ(c.Size(), 3u);
}

TEST(IconCacheTest, IconBearingPathCaseInsensitiveKey) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\a\\WeChat.EXE");
    c.Get(L"D:\\a\\wechat.exe");
    EXPECT_EQ(calls, 1);   // 同一路径（大小写无关）只加载一次
}

TEST(IconCacheTest, NonIconExtensionStillSharesByKey) {
    int calls = 0;
    IconCache c(CountingLoader(calls), 16);
    c.Get(L"D:\\a\\x.png");
    c.Get(L"D:\\b\\y.png");
    EXPECT_EQ(calls, 1);   // 非图标类维持扩展名共享（内存优化约束不回退）
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
