// 模块 2 测试：数据模型编译验证 + IndexStore 行为
// 设计依据: doc/code-prompt.md §六 Module2 / doc/detailed-design.md §4
#include <gtest/gtest.h>

#include "provider/IProvider.h"
#include "provider/ISearchableProvider.h"
#include "provider/IndexStore.h"

using namespace iris;

// 编译期约束（与头文件内 static_assert 一致，再次确认生效）
static_assert(sizeof(DirEntry) == 10, "DirEntry 必须 10 字节");
static_assert(sizeof(CompactFileEntry) == 14, "CompactFileEntry 必须 14 字节");

namespace {
CompactFileEntry MakeEntry(StringPool& names, uint16_t dirId,
                           const std::wstring& name) {
    CompactFileEntry e{};
    e.nameOffset = names.Insert(name);
    e.nameLength = static_cast<uint16_t>(name.size());
    e.type       = static_cast<uint32_t>(ItemType::FILE);
    e.dirId      = dirId;
    e.pathDepth  = 1;
    e.flags      = 0;
    e.usnLow     = 0;
    return e;
}
} // namespace

TEST(IndexStoreTest, InsertSortAndFindPrefix) {
    IndexStore store;
    StringPool& names = store.NamePool();
    StringPool& dirs  = store.DirPool();
    const uint16_t dirId = store.AddDir({dirs.Insert(L"D:\\docs"), 7, UINT32_MAX});

    store.Insert(MakeEntry(names, dirId, L"report.docx"));
    store.Insert(MakeEntry(names, dirId, L"apple.txt"));
    store.Insert(MakeEntry(names, dirId, L"banana.txt"));
    store.Insert(MakeEntry(names, dirId, L"react.js"));
    store.Sort();

    // 排序后顺序应为 apple, banana, react, report；前缀 "re" 定位到 react.js
    const std::size_t idx = store.FindFirstPrefix(L"re");
    ASSERT_LT(idx, store.size());
    EXPECT_EQ(std::wstring(store.NameAt(store[idx])), L"react.js");
    EXPECT_EQ(std::wstring(store.NameAt(store[0])), L"apple.txt");
}

TEST(IndexStoreTest, ToResultItemBuildsFullPath) {
    IndexStore store;
    StringPool& names = store.NamePool();
    StringPool& dirs  = store.DirPool();
    const uint16_t dirId = store.AddDir({dirs.Insert(L"D:\\docs"), 7, UINT32_MAX});
    store.Insert(MakeEntry(names, dirId, L"report.docx"));

    const ResultItem r = store.ToResultItem(store[0]);
    EXPECT_EQ(r.title, L"report.docx");
    EXPECT_EQ(r.subtitle, L"D:\\docs");
    EXPECT_EQ(r.path, L"D:\\docs\\report.docx");
    EXPECT_EQ(r.type, ItemType::FILE);
    EXPECT_EQ(r.pathDepth, 1u);
}

TEST(IndexStoreTest, EmptyPrefixReturnsZero) {
    IndexStore store;
    EXPECT_EQ(store.FindFirstPrefix(L""), 0u);
    EXPECT_EQ(store.size(), 0u);
}
