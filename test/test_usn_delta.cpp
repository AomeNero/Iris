// UsnRecordParser + UsnDelta 测试
// Parser 契约：解析 USN_RECORD_V2 字节流 → UsnEvent；缓冲不足/版本不支持/
//             名字越界返回 0（失败不越界）。
// Delta 契约：CREATE→Upsert、DELETE→Remove（同帧内新建又删除净零）、
//             RENAME_OLD 记 pending、RENAME_NEW 生成新名 Upsert、
//             DATA_OVERWRITE 忽略、BASIC_INFO 更新属性；ShouldCompact 按条数/滞留时长。
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include "provider/UsnDelta.h"
#include "provider/UsnRecordParser.h"

using namespace iris;

namespace {

// 手工构造一条 USN_RECORD_V2（RecordLength 按 8 字节对齐）
std::vector<uint8_t> MakeRecord(uint64_t frn, uint64_t parent, uint32_t reason,
                                uint32_t attrs, const std::wstring& name) {
    const size_t nameBytes = name.size() * sizeof(wchar_t);
    std::vector<uint8_t> r(sizeof(USN_RECORD_V2) + nameBytes + 8);
    auto* rec = reinterpret_cast<USN_RECORD_V2*>(r.data());
    rec->RecordLength = static_cast<DWORD>((sizeof(USN_RECORD_V2) + nameBytes + 7) & ~size_t{7});
    rec->MajorVersion = 2;
    rec->MinorVersion = 0;
    rec->FileReferenceNumber = frn;
    rec->ParentFileReferenceNumber = parent;
    rec->Usn = 0;
    rec->Reason = reason;
    rec->SourceInfo = 0;
    rec->SecurityId = 0;
    rec->FileAttributes = attrs;
    rec->FileNameLength = static_cast<WORD>(nameBytes);
    rec->FileNameOffset = static_cast<WORD>(sizeof(USN_RECORD_V2));
    std::memcpy(r.data() + sizeof(USN_RECORD_V2), name.c_str(), nameBytes);
    r.resize(rec->RecordLength);
    return r;
}

UsnEvent Ev(uint64_t frn, uint32_t reason, const std::wstring& name = L"x",
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

// ── ParseUsnRecord ──

TEST(UsnRecordParserTest, ParsesCreateEvent) {
    const auto rec = MakeRecord(0xABC, 0xDEF, USN_REASON_FILE_CREATE,
                                FILE_ATTRIBUTE_ARCHIVE, L"hello.txt");
    UsnEvent e;
    const size_t used = ParseUsnRecord(rec.data(), rec.size(), e);
    EXPECT_EQ(used, rec.size());
    EXPECT_EQ(e.frn, 0xABCu);
    EXPECT_EQ(e.parentFrn, 0xDEFu);
    EXPECT_EQ(e.reason, static_cast<uint32_t>(USN_REASON_FILE_CREATE));
    EXPECT_EQ(e.attributes, static_cast<uint32_t>(FILE_ATTRIBUTE_ARCHIVE));
    EXPECT_EQ(e.name, L"hello.txt");
}

TEST(UsnRecordParserTest, ParsesTwoConsecutiveRecords) {
    const auto r1 = MakeRecord(1, 2, USN_REASON_FILE_CREATE, 0x20, L"a.txt");
    const auto r2 = MakeRecord(3, 2, USN_REASON_FILE_DELETE, 0x20, L"b.txt");
    std::vector<uint8_t> buf(r1);
    buf.insert(buf.end(), r2.begin(), r2.end());

    UsnEvent e;
    const size_t off = ParseUsnRecord(buf.data(), buf.size(), e);
    ASSERT_EQ(off, r1.size());
    EXPECT_EQ(e.name, L"a.txt");

    const size_t off2 = ParseUsnRecord(buf.data() + off, buf.size() - off, e);
    ASSERT_EQ(off2, r2.size());
    EXPECT_EQ(e.name, L"b.txt");
    EXPECT_EQ(e.reason, static_cast<uint32_t>(USN_REASON_FILE_DELETE));
}

TEST(UsnRecordParserTest, RejectsTruncatedRecord) {
    const auto rec = MakeRecord(1, 2, USN_REASON_FILE_CREATE, 0x20, L"a.txt");
    UsnEvent e;
    EXPECT_EQ(ParseUsnRecord(rec.data(), rec.size() - 1, e), 0u);
}

TEST(UsnRecordParserTest, RejectsBufferShorterThanHeader) {
    UsnEvent e;
    EXPECT_EQ(ParseUsnRecord(nullptr, sizeof(USN_RECORD_V2) - 1, e), 0u);
}

TEST(UsnRecordParserTest, RejectsUnsupportedVersion) {
    auto rec = MakeRecord(1, 2, USN_REASON_FILE_CREATE, 0x20, L"a.txt");
    reinterpret_cast<USN_RECORD_V2*>(rec.data())->MajorVersion = 3;
    UsnEvent e;
    EXPECT_EQ(ParseUsnRecord(rec.data(), rec.size(), e), 0u);
}

TEST(UsnRecordParserTest, RejectsNameOutOfBounds) {
    auto rec = MakeRecord(1, 2, USN_REASON_FILE_CREATE, 0x20, L"a.txt");
    auto* recPtr = reinterpret_cast<USN_RECORD_V2*>(rec.data());
    recPtr->FileNameLength = 0xFFFF;  // 越出 RecordLength
    UsnEvent e;
    EXPECT_EQ(ParseUsnRecord(rec.data(), rec.size(), e), 0u);
}

// ── UsnDelta ──

TEST(UsnDeltaTest, CreateBecomesUpsert) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_FILE_CREATE, L"new.txt"), 100);
    ASSERT_EQ(d.Size(), 1u);
    const auto& c = d.Items().at(7);
    EXPECT_EQ(c.kind, UsnDelta::Kind::Upsert);
    EXPECT_EQ(c.name, L"new.txt");
    EXPECT_EQ(c.parentFrn, 1u);
}

TEST(UsnDeltaTest, DeleteBecomesRemove) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_FILE_DELETE), 100);
    ASSERT_EQ(d.Size(), 1u);
    EXPECT_EQ(d.Items().at(7).kind, UsnDelta::Kind::Remove);
}

// 快照加载后新建又删除的文件：净效果为零，不应滞留 delta
TEST(UsnDeltaTest, CreateThenDeleteNetsEmpty) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_FILE_CREATE), 100);
    d.Apply(Ev(7, USN_REASON_FILE_DELETE | USN_REASON_CLOSE), 110);
    EXPECT_TRUE(d.Empty());
}

TEST(UsnDeltaTest, RenameWithOldProducesUpsertNewName) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_RENAME_OLD_NAME, L"old.txt"), 100);
    EXPECT_TRUE(d.Empty());  // 旧名仅 pending，不产生变更
    d.Apply(Ev(7, USN_REASON_RENAME_NEW_NAME, L"new.txt"), 110);
    ASSERT_EQ(d.Size(), 1u);
    EXPECT_EQ(d.Items().at(7).kind, UsnDelta::Kind::Upsert);
    EXPECT_EQ(d.Items().at(7).name, L"new.txt");
}

// 无 OLD 记录的 NEW（外部移动进入本卷）→ 直接 Upsert
TEST(UsnDeltaTest, RenameNewWithoutPendingIsUpsert) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_RENAME_NEW_NAME, L"moved.txt"), 100);
    ASSERT_EQ(d.Size(), 1u);
    EXPECT_EQ(d.Items().at(7).name, L"moved.txt");
}

TEST(UsnDeltaTest, DataOverwriteIgnored) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_DATA_OVERWRITE), 100);
    EXPECT_TRUE(d.Empty());
}

TEST(UsnDeltaTest, BasicInfoChangeUpdatesAttributes) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_FILE_CREATE, L"x", 0x20), 100);
    d.Apply(Ev(7, USN_REASON_BASIC_INFO_CHANGE, L"x", 0x22), 110);
    ASSERT_EQ(d.Size(), 1u);
    EXPECT_EQ(d.Items().at(7).attributes, 0x22u);
}

TEST(UsnDeltaTest, ShouldCompactByCount) {
    UsnDelta d(UsnDelta::Policy{/*maxChanges=*/3, /*maxAgeMs=*/1000});
    EXPECT_FALSE(d.ShouldCompact(0));
    d.Apply(Ev(1, USN_REASON_FILE_CREATE), 10);
    d.Apply(Ev(2, USN_REASON_FILE_CREATE), 10);
    d.Apply(Ev(3, USN_REASON_FILE_CREATE), 10);
    EXPECT_TRUE(d.ShouldCompact(11));
}

TEST(UsnDeltaTest, ShouldCompactByAge) {
    UsnDelta d(UsnDelta::Policy{/*maxChanges=*/100, /*maxAgeMs=*/1000});
    d.Apply(Ev(1, USN_REASON_FILE_CREATE), 1000);
    EXPECT_FALSE(d.ShouldCompact(1500));
    EXPECT_TRUE(d.ShouldCompact(2001));
}

TEST(UsnDeltaTest, ShouldCompactNotWhenEmpty) {
    UsnDelta d(UsnDelta::Policy{/*maxChanges=*/100, /*maxAgeMs=*/5});
    EXPECT_FALSE(d.ShouldCompact(1000000));  // 空 delta 永不触发
}

TEST(UsnDeltaTest, ClearResetsAll) {
    UsnDelta d;
    d.Apply(Ev(7, USN_REASON_RENAME_OLD_NAME, L"old.txt"), 100);
    d.Apply(Ev(8, USN_REASON_FILE_CREATE), 100);
    d.Clear();
    EXPECT_TRUE(d.Empty());
    EXPECT_FALSE(d.ShouldCompact(1000000));
    // 清空后 pending 也应失效：OLD 已清，NEW 直接 Upsert
    d.Apply(Ev(7, USN_REASON_RENAME_NEW_NAME, L"new.txt"), 200);
    EXPECT_EQ(d.Items().at(7).name, L"new.txt");
}
