// TrigramIndex 测试：候选集语义（必要条件交集）
// 契约：Query 返回「含全部查询 trigram」的候选集（允许假阳性）；
//       精确验证下沉 Matcher（MatchSingle 经 GetSearchText 完整验证）。
#include <gtest/gtest.h>

#include "provider/IndexStore.h"

using namespace iris;

namespace {

// 经真 IndexStore 构建 trigram（生产同路径：Sort + BuildTrigramIndex）
struct StoreFixture {
    IndexStore store;
    StringPool& names = store.NamePool();
    StringPool& dirs  = store.DirPool();

    void Add(uint16_t dirId, const std::wstring& name,
             ItemType type = ItemType::FILE) {
        CompactFileEntry e{};
        e.nameOffset = names.Insert(name);
        e.nameLength = static_cast<uint16_t>(name.size());
        e.type       = static_cast<uint32_t>(type);
        e.dirId      = dirId;
        e.pathDepth  = 1;
        e.flags      = 0;
        e.usnLow     = 0;
        store.Insert(e);
    }
    uint16_t AddDir(const std::wstring& path) {
        return store.AddDir(
            {dirs.Insert(path), static_cast<uint16_t>(path.size()), UINT32_MAX});
    }
    void Finalize() {
        store.Sort();
        store.BuildTrigramIndex();
    }
};

} // namespace

TEST(TrigramQueryTest, EntryCountMatchesStore) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Add(dirId, L"banana.txt");
    f.Finalize();
    EXPECT_EQ(f.store.GetTrigramIndex().EntryCount(), 2u);
}

TEST(TrigramQueryTest, QueryReturnsExactHits) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Add(dirId, L"banana.txt");
    f.Finalize();

    const auto r = f.store.GetTrigramIndex().Query(L"apple");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 0u);  // Sort 后 apple.txt 在前
}

TEST(TrigramQueryTest, QueryIntersectsAllTrigrams) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Add(dirId, L"banana.txt");
    f.Finalize();

    // "ple" 的 trigram 只出现在 apple.txt
    const auto r = f.store.GetTrigramIndex().Query(L"ple");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 0u);
}

// 新契约核心：Query 只做 trigram 交集（必要条件），允许假阳性通过。
// 文本 "abc.txt d:\bcd" 含 trigram abc、bcd 但不含完整 abcd —— 必须保留为候选。
TEST(TrigramQueryTest, ReturnsCandidateSetAllowingFalsePositives) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\bcd");
    f.Add(dirId, L"abc.txt");
    f.Finalize();

    const auto r = f.store.GetTrigramIndex().Query(L"abcd");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 0u);
}

TEST(TrigramQueryTest, QueryShorterThan3ReturnsAll) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Add(dirId, L"banana.txt");
    f.Finalize();

    const auto r = f.store.GetTrigramIndex().Query(L"ap");
    EXPECT_EQ(r.size(), 2u);  // 退化为全量候选
}

TEST(TrigramQueryTest, QueryNoTrigramHitReturnsEmpty) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Finalize();

    EXPECT_TRUE(f.store.GetTrigramIndex().Query(L"zzz").empty());
}

TEST(TrigramQueryTest, QueryCaseInsensitive) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\bcd");
    f.Add(dirId, L"abc.txt");
    f.Finalize();

    const auto r = f.store.GetTrigramIndex().Query(L"ABC");
    EXPECT_EQ(r.size(), 1u);
}

TEST(TrigramQueryTest, RebuildYieldsSameResults) {
    StoreFixture f;
    const uint16_t dirId = f.AddDir(L"D:\\a");
    f.Add(dirId, L"apple.txt");
    f.Add(dirId, L"banana.txt");
    f.Finalize();
    const auto first = f.store.GetTrigramIndex().Query(L"apple");

    // 重建（compact 场景：同一 store 反复 Build）结果必须一致
    f.Finalize();
    const auto second = f.store.GetTrigramIndex().Query(L"apple");
    EXPECT_EQ(first, second);
}
