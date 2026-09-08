// Iris Provider 层 —— USN 记录解析实现
#include "provider/UsnRecordParser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstring>

namespace iris {

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
