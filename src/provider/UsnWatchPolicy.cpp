// Iris Provider —— USN 监听错误分派策略实现
#include "provider/UsnWatchPolicy.h"

namespace iris {

UsnErrorAction ClassifyUsnError(uint32_t err) {
    switch (err) {
        case kUsnErrHandleEof:            return UsnErrorAction::Ignore;        // 无增量
        case kUsnErrAccessDenied:         return UsnErrorAction::DisableVolume; // 权限类：禁用，绝不重建
        case kUsnErrInvalidHandle:        return UsnErrorAction::ReopenOnce;    // 句柄失效：重开一次
        case kUsnErrJournalDelete:        return UsnErrorAction::Rebuild;       // journal 被删
        case kUsnErrJournalEntryDeleted:  return UsnErrorAction::Rebuild;       // 游标落后被覆盖
        default:                          return UsnErrorAction::Backoff;       // 未知：退避观察
    }
}

} // namespace iris
