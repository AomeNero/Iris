// DeltaOverlay 测试：基础索引 + USN delta 的统一 entry 空间
// 编码契约：[0, baseSize) = 基础索引；[baseSize, baseSize+upsertCount) = delta upsert
//（按 frn 升序稳定排序）；Remove 不占空间（基础条目经 IsRemoved 隐藏）。
#include <gtest/gtest.h>

#include <utility>

#include "provider/DeltaOverlay.h"

using namespace iris;

namespace {

struct BaseBuilder {
    IndexStore store;
    StringPool& names = store.NamePool();
    StringPool& dirs  = store.DirPool();

    uint16_t AddDir(const std::wstring& path) {
        return store.AddDir(
            {dirs.Insert(path), static_cast<uint16_t>(path.size()), UINT32_MAX});
    }
    void Add(uint16_t dirId, const std::wstring& name, uint32_t frn = 0) {
        CompactFileEntry e{};
        e.nameOffset = names.Insert(name);
        e.nameLength = static_cast<uint16_t>(name.size());
        e.type       = static_cast<uint32_t>(ItemType::FILE);
        e.dirId      = dirId;
        e.pathDepth  = 1;
        e.flags      = 0;
        e.usnLow     = frn;  // 与生产一致：FRN 低 32 位（FileProvider.cpp 同约定）
        store.Insert(e);
    }
    void Finalize() {
        store.Sort();
        store.BuildTrigramIndex();
    }
};

UsnEvent Ev(uint64_t frn, uint32_t reason, const std::wstring& name,
            uint32_t attrs = 0x20, uint64_t parent = 1) {
    UsnEvent e;
    e.frn = frn;
    e.parentFrn = parent;
    e.reason = reason;
    e.attributes = attrs;
    e.name = name;
    return e;
}

} // namespace

TEST(DeltaOverlayTest, SizeIncludesUpsertsNotRemoves) {
    BaseBuilder b;
    const uint16_t d = b.AddDir(L"D:\\a");
    b.Add(d, L"apple.txt");   // Sort 后 index 0
    b.Add(d, L"banana.txt");  // index 1
    b.Finalize();

    UsnDelta delta;
    delta.Apply(Ev(100, kUsnReasonFileCreate, L"new.txt"), 100);
    delta.Apply(Ev(101, kUsnReasonFileCreate, L"fresh.txt"), 100);
    delta.Apply(Ev(102, kUsnReasonFileDelete, L"gone.txt"), 100);  // 纯 Remove 不占空间

    DeltaOverlay ov(std::make_shared<IndexStore>(std::move(b.store)), std::move(delta));
    EXPECT_EQ(ov.Size(), 4u);  // 2 base + 2 upsert
}

TEST(DeltaOverlayTest, IsDeltaAndChangeLookup) {
    BaseBuilder b;
    const uint16_t d = b.AddDir(L"D:\\a");
    b.Add(d, L"apple.txt");
    b.Finalize();

    UsnDelta delta;
    delta.Apply(Ev(100, kUsnReasonFileCreate, L"new.txt"), 100);

    DeltaOverlay ov(std::make_shared<IndexStore>(std::move(b.store)), std::move(delta));
    EXPECT_FALSE(ov.IsDelta(0));           // base 条目
    EXPECT_TRUE(ov.IsDelta(1));            // 第一个 upsert（delta 空间 relIdx=0）
    EXPECT_EQ(ov.DeltaChange(0).name, L"new.txt");
    EXPECT_EQ(ov.DeltaChange(0).kind, UsnDelta::Kind::Upsert);
}

// 删除的 base 条目被标记隐藏；未删除的保持可见（FRN 低 32 位近似匹配）
TEST(DeltaOverlayTest, IsRemovedMarksDeletedBaseEntries) {
    BaseBuilder b;
    const uint16_t d = b.AddDir(L"D:\\a");
    b.Add(d, L"apple.txt", 0x1111);    // base index 0（usnLow=0x1111）
    b.Add(d, L"banana.txt", 0x2222);   // base index 1
    b.Finalize();

    UsnDelta delta;
    delta.Apply(Ev(0x1111, kUsnReasonFileDelete, L"apple.txt"), 100);

    DeltaOverlay ov(std::make_shared<IndexStore>(std::move(b.store)), std::move(delta));
    EXPECT_TRUE(ov.IsRemoved(0));
    EXPECT_FALSE(ov.IsRemoved(1));
}

// delta 子串候选：v1 仅按 name 文本匹配（大小写不敏感；父目录 FRN 无路径映射）
TEST(DeltaOverlayTest, DeltaSubstringMatchesName) {
    BaseBuilder b;
    const uint16_t d = b.AddDir(L"D:\\a");
    b.Add(d, L"apple.txt");
    b.Finalize();

    UsnDelta delta;
    delta.Apply(Ev(100, kUsnReasonFileCreate, L"Readme.md"), 100);
    delta.Apply(Ev(101, kUsnReasonFileCreate, L"other.log"), 100);

    DeltaOverlay ov(std::make_shared<IndexStore>(std::move(b.store)), std::move(delta));
    const auto hitName = ov.DeltaSubstringCandidates(L"readme");
    ASSERT_EQ(hitName.size(), 1u);
    EXPECT_EQ(ov.DeltaChange(hitName[0]).name, L"Readme.md");
    EXPECT_TRUE(ov.DeltaSubstringCandidates(L"zzz").empty());
}
