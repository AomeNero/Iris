// Iris Provider 层 —— USN 记录解析实现
#include "provider/UsnRecordParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace iris {

// FileProvider 与 UsnDelta 关于 V2/V3 的两条推理依赖以下 SDK 事实，
// 钉住以防 SDK 变更后推理静默失效：
//   1. USN_RECORD 只是 USN_RECORD_V2 的别名——两处调用本是同一类型，
//      差别只在校验逻辑（这也是能删掉重复实现、直接共用本函数的前提）。
//   2. V2/V3 的公共头（RecordLength/MajorVersion/MinorVersion）位置相同，
//      故按 RecordLength 推进缓冲与版本无关；而 V3 其余字段整体后移 16 字节
//      （两个 128 位文件引用），这正是"按 V2 解读 V3 会读错 FileNameOffset"
//      的依据。
static_assert(std::is_same_v<USN_RECORD, USN_RECORD_V2>,
              "USN_RECORD 须为 USN_RECORD_V2 的别名");
static_assert(offsetof(USN_RECORD_V3, MajorVersion) ==
                  offsetof(USN_RECORD_V2, MajorVersion),
              "V2/V3 公共头位置须一致");
static_assert(offsetof(USN_RECORD_V3, FileNameOffset) ==
                  offsetof(USN_RECORD_V2, FileNameOffset) + 16,
              "USN_RECORD_V3 须整体后移 16 字节");

size_t ParseUsnRecord(const uint8_t* buf, size_t avail, UsnEvent& out) {
    if (buf == nullptr || avail < sizeof(USN_RECORD_V2)) return 0;
    const auto* rec = reinterpret_cast<const USN_RECORD_V2*>(buf);
    if (rec->RecordLength < sizeof(USN_RECORD_V2)) return 0;
    if (rec->RecordLength > avail) return 0;
    if (rec->MajorVersion != 2) return 0;  // 仅支持 V2（V3 为 64 位句柄变体，暂拒绝）

    const size_t nameOff = rec->FileNameOffset;
    const size_t nameBytes = rec->FileNameLength;
    if (nameOff + nameBytes > rec->RecordLength) return 0;

    out.frn = rec->FileReferenceNumber;
    out.parentFrn = rec->ParentFileReferenceNumber;
    out.reason = rec->Reason;
    out.attributes = rec->FileAttributes;
    out.name.assign(reinterpret_cast<const wchar_t*>(buf + nameOff),
                    nameBytes / sizeof(wchar_t));
    return rec->RecordLength;
}

} // namespace iris
