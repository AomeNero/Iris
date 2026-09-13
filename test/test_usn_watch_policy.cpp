// UsnWatchPolicy + RebuildBreaker 测试
// 分派契约：EOF→忽略、ACCESS_DENIED→禁用卷、INVALID_HANDLE→重开一次、
//           JOURNAL_DELETE/ENTRY_DELETED→重建、其余→退避（连续 5 次失败禁用）。
// 熔断契约：滑动窗口内上限 N 次；拒绝不消费额度；旧记录滑出窗口自然恢复。
//           触发方先 ShouldAllow 再 RecordRebuild。
#include <gtest/gtest.h>

#include "provider/RebuildBreaker.h"
#include "provider/UsnWatchPolicy.h"

using iris::ClassifyUsnError;
using iris::kUsnErrAccessDenied;
using iris::kUsnErrHandleEof;
using iris::kUsnErrInvalidHandle;
using iris::kUsnErrJournalDelete;
using iris::kUsnErrJournalEntryDeleted;
using iris::RebuildBreaker;
using iris::ShouldPollVolume;
using iris::UsnErrorAction;

// ---- 错误分派表（全量重建只留给确凿的 journal 失效）----

TEST(UsnWatchPolicy, HandleEofMeansNoDelta) {
    EXPECT_EQ(ClassifyUsnError(kUsnErrHandleEof), UsnErrorAction::Ignore);
}

TEST(UsnWatchPolicy, AccessDeniedDisablesVolumeNeverRebuilds) {
    // 非提权开不出卷句柄 → 必须禁用监听，而非重建（v1.4.0 重建风暴根因）
    EXPECT_EQ(ClassifyUsnError(kUsnErrAccessDenied), UsnErrorAction::DisableVolume);
}

TEST(UsnWatchPolicy, InvalidHandleReopensOnce) {
    EXPECT_EQ(ClassifyUsnError(kUsnErrInvalidHandle), UsnErrorAction::ReopenOnce);
}

TEST(UsnWatchPolicy, JournalDeleteIsCertainInvalidation) {
    EXPECT_EQ(ClassifyUsnError(kUsnErrJournalDelete), UsnErrorAction::Rebuild);
}

TEST(UsnWatchPolicy, JournalEntryDeletedIsCertainInvalidation) {
    EXPECT_EQ(ClassifyUsnError(kUsnErrJournalEntryDeleted), UsnErrorAction::Rebuild);
}

TEST(UsnWatchPolicy, UnknownErrorBacksOff) {
    EXPECT_EQ(ClassifyUsnError(87), UsnErrorAction::Backoff);  // ERROR_INVALID_PARAMETER
    EXPECT_EQ(ClassifyUsnError(0xFFFFFFFFu), UsnErrorAction::Backoff);
}

// ---- 卷轮询门控（FallbackScan 卷默认禁用，天然跳过）----

TEST(UsnWatchPolicy, PollGate) {
    EXPECT_TRUE(ShouldPollVolume(true, false));    // NTFS 正常卷
    EXPECT_FALSE(ShouldPollVolume(true, true));    // 已禁用（权限/句柄不可恢复）
    EXPECT_FALSE(ShouldPollVolume(false, false));  // 非 NTFS 无 USN
    EXPECT_FALSE(ShouldPollVolume(false, true));
}

// ---- 熔断器 ----

TEST(RebuildBreaker, AllowsUpToLimitThenRefuses) {
    RebuildBreaker b(3, 60000);
    EXPECT_TRUE(b.ShouldAllow(1000)); b.RecordRebuild(1000);
    EXPECT_TRUE(b.ShouldAllow(2000)); b.RecordRebuild(2000);
    EXPECT_TRUE(b.ShouldAllow(3000)); b.RecordRebuild(3000);
    EXPECT_FALSE(b.ShouldAllow(4000));   // 第 4 次：窗口内已达 3 次
    EXPECT_FALSE(b.ShouldAllow(60000));  // 记录仍未滑出窗口
}

TEST(RebuildBreaker, WindowSlidesAndRecovers) {
    RebuildBreaker b(3, 60000);
    b.RecordRebuild(1000);
    b.RecordRebuild(2000);
    b.RecordRebuild(3000);
    EXPECT_FALSE(b.ShouldAllow(60000));
    // t=1000 的记录在 t=61000 滑出窗口（61000-1000 >= 60000）
    EXPECT_TRUE(b.ShouldAllow(61000));
}

TEST(RebuildBreaker, RefusalDoesNotConsumeQuotaAndRefills) {
    RebuildBreaker b(2, 60000);
    b.RecordRebuild(1000);
    b.RecordRebuild(2000);
    EXPECT_FALSE(b.ShouldAllow(3000));
    EXPECT_FALSE(b.ShouldAllow(4000));  // 连续拒绝不追加记录
    EXPECT_TRUE(b.ShouldAllow(62000));  // 全部滑出，额度恢复
    b.RecordRebuild(62000);
    b.RecordRebuild(62100);
    EXPECT_FALSE(b.ShouldAllow(62200)); // 新窗口再度打满
}

TEST(RebuildBreaker, BoundaryExactWindowExpires) {
    RebuildBreaker b(1, 1000);
    b.RecordRebuild(5000);
    EXPECT_FALSE(b.ShouldAllow(5999));  // 5999-5000 < 1000，仍在窗口
    EXPECT_TRUE(b.ShouldAllow(6000));   // 6000-5000 >= 1000，恰好过期
}
