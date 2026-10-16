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

// BuildVolumeIndex 构建期同时持有约 284 万份本结构（FRN → UsnEvent 表），
// 故钉住尺寸预算：加字段会在此编译失败，逼一次有意识的决定。
static_assert(sizeof(UsnEvent) <= 64, "UsnEvent 尺寸预算：构建期约 284 万份");

/// 解析一条 USN_RECORD_V2。成功填充 out 并返回记录消耗的字节数（>0）；
/// 缓冲不足 / MajorVersion != 2 / FileName 越界返回 0。
/// buf 须 8 字节对齐（USN 系统缓冲天然满足）。
size_t ParseUsnRecord(const uint8_t* buf, size_t avail, UsnEvent& out);

} // namespace iris
