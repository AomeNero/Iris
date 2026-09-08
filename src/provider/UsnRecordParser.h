// Iris Provider 层 —— USN 记录解析（纯函数，无卷 I/O）
// 契约：解析单条 USN_RECORD_V2 成结构化事件；缓冲不足/版本不支持/
//       名字越界返回 0（调用方据此停止或推进）。不产生任何副作用。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace iris {

struct UsnEvent {
    uint64_t     frn = 0;        // FileReferenceNumber
    uint64_t     parentFrn = 0;  // 父目录 FRN
    uint32_t     reason = 0;     // USN_REASON_* 位组合（原始值）
    uint32_t     attributes = 0; // FILE_ATTRIBUTE_* 位组合
    std::wstring name;
};

/// 解析一条 USN_RECORD_V2。成功填充 out 并返回记录消耗的字节数（>0）；
/// 缓冲不足 / MajorVersion != 2 / FileName 越界返回 0。
/// buf 须 8 字节对齐（USN 系统缓冲天然满足）。
size_t ParseUsnRecord(const uint8_t* buf, size_t avail, UsnEvent& out);

} // namespace iris
