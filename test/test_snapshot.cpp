// IndexSnapshot 测试：快照 Save/Load round-trip 等价 + 损坏拒绝
// 契约：Save→Load 得到等价索引（条目/目录/池/trigram 查询行为一致）；
//       坏 magic/坏版本/截断/文件缺失 → Load 返回 nullptr，不崩溃。
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "provider/IndexStore.h"
#include "provider/IndexSnapshot.h"

using namespace iris;

namespace {

// 经真构建路径（Insert → Sort → BuildTrigramIndex）搭建 IndexStore
struct StoreBuilder {
    IndexStore store;
    StringPool& names = store.NamePool();
    StringPool& dirs  = store.DirPool();

    uint16_t AddDir(const std::wstring& path) {
        return store.AddDir(
            {dirs.Insert(path), static_cast<uint16_t>(path.size()), UINT32_MAX});
    }
    void Add(uint16_t dirId, const std::wstring& name,
             ItemType type = ItemType::FILE, uint8_t flags = 0) {
        CompactFileEntry e{};
        e.nameOffset = names.Insert(name);
        e.nameLength = static_cast<uint16_t>(name.size());
        e.type       = static_cast<uint32_t>(type);
        e.dirId      = dirId;
        e.pathDepth  = 2;
        e.flags      = flags;
        e.usnLow     = 0xABCD;
        store.Insert(e);
    }
    void Finalize() {
        store.Sort();
        store.BuildTrigramIndex();
    }
};

std::filesystem::path TempFile(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(p);
    return p;
}

void WriteBytes(const std::filesystem::path& p, const std::string& bytes) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(ifs),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST(IndexSnapshotTest, RoundTripPreservesEntries) {
    StoreBuilder b;
    const uint16_t d1 = b.AddDir(L"D:\\docs");
    const uint16_t d2 = b.AddDir(L"C:\\Users\\a\\AppData");
    b.Add(d1, L"report.docx");
    b.Add(d1, L"隐藏.sys", ItemType::FILE, 0x03);
    b.Add(d2, L"weixin.exe", ItemType::APPLICATION);
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_entries.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));

    const auto loaded = IndexSnapshot::Load(path);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->size(), b.store.size());
    EXPECT_EQ(loaded->DirCount(), b.store.DirCount());
    for (size_t i = 0; i < b.store.size(); ++i) {
        EXPECT_EQ(loaded->NameAt((*loaded)[i]), b.store.NameAt(b.store[i]));
        EXPECT_EQ(loaded->DirOf((*loaded)[i]), b.store.DirOf(b.store[i]));
        EXPECT_EQ((*loaded)[i].type, b.store[i].type);
        EXPECT_EQ((*loaded)[i].flags, b.store[i].flags);
        EXPECT_EQ((*loaded)[i].pathDepth, b.store[i].pathDepth);
        EXPECT_EQ((*loaded)[i].usnLow, b.store[i].usnLow);
    }
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, RoundTripPreservesTrigramQueries) {
    StoreBuilder b;
    const uint16_t d1 = b.AddDir(L"D:\\bcd");
    const uint16_t d2 = b.AddDir(L"D:\\a");
    b.Add(d1, L"abc.txt");
    b.Add(d2, L"apple.txt");
    b.Add(d2, L"banana.txt");
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_trigram.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));
    const auto loaded = IndexSnapshot::Load(path);
    ASSERT_NE(loaded, nullptr);

    const auto& t1 = b.store.GetTrigramIndex();
    const auto& t2 = loaded->GetTrigramIndex();
    EXPECT_EQ(t1.EntryCount(), t2.EntryCount());
    // 精确命中
    EXPECT_EQ(t2.Query(L"apple"), t1.Query(L"apple"));
    // 候选集语义（假阳性）在重载后同样成立
    EXPECT_EQ(t2.Query(L"abcd"), t1.Query(L"abcd"));
    // 无命中
    EXPECT_TRUE(t2.Query(L"zzz").empty());
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, RoundTripEmptyStore) {
    StoreBuilder b;
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_empty.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));
    const auto loaded = IndexSnapshot::Load(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->size(), 0u);
    EXPECT_EQ(loaded->DirCount(), 0u);
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, SaveOverwritesExisting) {
    StoreBuilder b;
    const uint16_t d = b.AddDir(L"D:\\v1");
    b.Add(d, L"old.txt");
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_overwrite.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));

    StoreBuilder b2;  // 不同内容覆盖同一文件
    const uint16_t d2 = b2.AddDir(L"D:\\v2");
    b2.Add(d2, L"new.txt");
    b2.Finalize();
    ASSERT_TRUE(IndexSnapshot::Save(b2.store, path));

    const auto loaded = IndexSnapshot::Load(path);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->size(), 1u);
    EXPECT_EQ(std::wstring(loaded->NameAt((*loaded)[0])), L"new.txt");
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, LoadRejectsBadMagic) {
    const auto path = TempFile("iris_test_snapshot_badmagic.idx");
    WriteBytes(path, std::string("XXXX") + std::string(64, '\0'));
    EXPECT_EQ(IndexSnapshot::Load(path), nullptr);
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, LoadRejectsBadVersion) {
    const auto path = TempFile("iris_test_snapshot_badver.idx");
    std::string bytes = std::string("IRIX");
    uint32_t ver = 999;
    bytes.append(reinterpret_cast<const char*>(&ver), 4);
    bytes.append(64, '\0');
    WriteBytes(path, bytes);
    EXPECT_EQ(IndexSnapshot::Load(path), nullptr);
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, LoadRejectsTruncated) {
    StoreBuilder b;
    const uint16_t d = b.AddDir(L"D:\\docs");
    b.Add(d, L"report.docx");
    b.Add(d, L"apple.txt");
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_trunc.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));
    std::string all = ReadAll(path);
    ASSERT_GT(all.size(), 16u);
    WriteBytes(path, all.substr(0, all.size() / 2));  // 截断一半
    EXPECT_EQ(IndexSnapshot::Load(path), nullptr);
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, LoadMissingFileReturnsNull) {
    const auto path = std::filesystem::temp_directory_path() /
                      "iris_test_snapshot_no_such_file.idx";
    std::filesystem::remove(path);
    EXPECT_EQ(IndexSnapshot::Load(path), nullptr);
}

// ── mmap 视图后端：同一套读接口断言，堆版与映射版结果必须一致 ──

TEST(IndexSnapshotTest, MappedViewMatchesHeapLoad) {
    StoreBuilder b;
    const uint16_t d1 = b.AddDir(L"D:\\docs");
    const uint16_t d2 = b.AddDir(L"D:\\a");
    b.Add(d1, L"report.docx");
    b.Add(d2, L"apple.txt", ItemType::FILE, 0x01);
    b.Add(d2, L"banana.txt");
    b.Finalize();

    const auto path = TempFile("iris_test_snapshot_mapped.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));

    const auto heap = IndexSnapshot::Load(path);
    const auto view = IndexSnapshot::LoadMapped(path);
    ASSERT_NE(heap, nullptr);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->IsView());

    // 条目级读接口逐一比对
    ASSERT_EQ(view->size(), heap->size());
    ASSERT_EQ(view->DirCount(), heap->DirCount());
    for (size_t i = 0; i < heap->size(); ++i) {
        EXPECT_EQ(view->NameAt((*view)[i]), heap->NameAt((*heap)[i]));
        EXPECT_EQ(view->DirOf((*view)[i]), heap->DirOf((*heap)[i]));
        EXPECT_EQ((*view)[i].type, (*heap)[i].type);
        EXPECT_EQ((*view)[i].flags, (*heap)[i].flags);
        EXPECT_EQ((*view)[i].usnLow, (*heap)[i].usnLow);
        EXPECT_EQ(view->ToResultItem((*view)[i]).path, heap->ToResultItem((*heap)[i]).path);
    }

    // trigram 查询行为一致（精确命中 / 候选集假阳性 / 短词全量）
    EXPECT_EQ(view->GetTrigramIndex().EntryCount(), heap->GetTrigramIndex().EntryCount());
    EXPECT_EQ(view->GetTrigramIndex().Query(L"apple"), heap->GetTrigramIndex().Query(L"apple"));
    EXPECT_EQ(view->GetTrigramIndex().Query(L"abcd"), heap->GetTrigramIndex().Query(L"abcd"));
    EXPECT_EQ(view->GetTrigramIndex().Query(L"ap"), heap->GetTrigramIndex().Query(L"ap"));

    // 前缀定位一致
    EXPECT_EQ(view->FindFirstPrefix(L"ban"), heap->FindFirstPrefix(L"ban"));
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, MappedViewRejectsBadMagic) {
    const auto path = TempFile("iris_test_snapshot_mapped_bad.idx");
    WriteBytes(path, std::string("XXXX") + std::string(64, '\0'));
    EXPECT_EQ(IndexSnapshot::LoadMapped(path), nullptr);
    std::filesystem::remove(path);
}

TEST(IndexSnapshotTest, MappedViewRejectsTruncated) {
    StoreBuilder b;
    const uint16_t d = b.AddDir(L"D:\\docs");
    b.Add(d, L"report.docx");
    b.Finalize();
    const auto path = TempFile("iris_test_snapshot_mapped_trunc.idx");
    ASSERT_TRUE(IndexSnapshot::Save(b.store, path));
    WriteBytes(path, ReadAll(path).substr(0, 24));  // 截到头部一半
    EXPECT_EQ(IndexSnapshot::LoadMapped(path), nullptr);
    std::filesystem::remove(path);
}
