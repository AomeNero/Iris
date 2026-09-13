// Iris Provider —— USN 监听错误分派策略（纯函数，无 I/O）
// 契约：全量重建只保留给"确凿的 journal 失效"（JOURNAL_DELETE/ENTRY_DELETED）；
//       权限/句柄类错误降级为禁用该卷监听（索引保持静态），绝不触发重建。
// 背景：非提权运行时卷句柄开不出（err=5），对空句柄轮询报 err=6——v1.4.0 曾把
//       这类错误一律送全量重建，造成 ~2 分钟一轮的永久重建循环（占满一核）。
#pragma once

#include <cstdint>

namespace iris {

// USN 相关 Win32 错误码（本地定义避免 windows.h 传染；值与 winerror.h 一致）
inline constexpr uint32_t kUsnErrAccessDenied        = 5;    // ERROR_ACCESS_DENIED
inline constexpr uint32_t kUsnErrInvalidHandle       = 6;    // ERROR_INVALID_HANDLE
inline constexpr uint32_t kUsnErrHandleEof           = 38;   // ERROR_HANDLE_EOF（无增量）
inline constexpr uint32_t kUsnErrJournalDelete       = 1178; // ERROR_JOURNAL_DELETE
inline constexpr uint32_t kUsnErrJournalEntryDeleted = 1179; // ERROR_JOURNAL_ENTRY_DELETED

enum class UsnErrorAction {
    Ignore,        // 无增量/正常（EOF）
    DisableVolume, // 禁用该卷监听（权限不足等永久性失败），索引保持静态
    ReopenOnce,    // 句柄失效：重开卷句柄一次，失败则禁用该卷
    Rebuild,       // journal 确凿失效 → 全量重建（经熔断器）
    Backoff,       // 未知错误：指数退避重试，连续失败达上限后禁用该卷
};

/// 把 FSCTL_READ_USN_JOURNAL 的失败错误码分派为处理动作。
UsnErrorAction ClassifyUsnError(uint32_t err);

/// 该卷是否应被监听线程轮询（FallbackScan 建卷时 usnDisabled=true，天然跳过）。
inline bool ShouldPollVolume(bool isNTFS, bool usnDisabled) {
    return isNTFS && !usnDisabled;
}

} // namespace iris
