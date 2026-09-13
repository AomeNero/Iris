// Iris Service —— 命名管道帧 I/O（重叠 + 截止时间；客户端/服务端共用）
// 契约：所有函数在超时/失败时取消挂起 I/O 并返回 false——会话应立即终止，
// 不允许任何调用在坏连接上无限阻塞（SYSTEM 服务的健壮性红线）。
#pragma once

#include "service/ServiceProtocol.h"

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace iris::usnbroker {

/// 带截止时间的整段读/写。completed = I/O 是否在时限内完整完成。
/// 返回 false = 超时或 I/O 错误（两种情况调用方都必须终止会话）。
inline bool PipeTransfer(HANDLE pipe, void* buf, DWORD size, bool write,
                         uint32_t timeoutMs, bool& completed) {
    completed = false;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    const BOOL started = write ? WriteFile(pipe, buf, size, nullptr, &ov)
                               : ReadFile(pipe, buf, size, nullptr, &ov);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return false;
    }
    const DWORD w = WaitForSingleObject(ov.hEvent, timeoutMs);
    if (w == WAIT_OBJECT_0) {
        DWORD transferred = 0;
        completed = GetOverlappedResult(pipe, &ov, &transferred, FALSE) &&
                    transferred == size;
    } else {
        CancelIoEx(pipe, &ov);  // 超时：取消挂起 I/O，防连接卡死占用服务
    }
    CloseHandle(ov.hEvent);
    return w == WAIT_OBJECT_0;
}

/// 编码并发送一帧。
inline bool PipeSendFrame(HANDLE pipe, uint16_t type, const void* payload,
                          uint32_t len, uint32_t timeoutMs) {
    std::vector<uint8_t> frame = EncodeFrame(type, payload, len);
    if (frame.empty()) return false;
    bool completed = false;
    return PipeTransfer(pipe, frame.data(), static_cast<DWORD>(frame.size()),
                        true, timeoutMs, completed) && completed;
}

/// 读取一帧并校验类型/载荷上限。len 进为缓冲容量，出为实际载荷字节数。
inline bool PipeRecvFrame(HANDLE pipe, uint16_t wantType, uint8_t* payload,
                          uint32_t& len, uint32_t timeoutMs) {
    uint8_t header[kFrameHeaderSize];
    bool completed = false;
    if (!PipeTransfer(pipe, header, kFrameHeaderSize, false, timeoutMs, completed) || !completed)
        return false;
    FrameHeader h;
    if (ParseFrameHeader(header, kFrameHeaderSize, h) != ParseResult::Ok) return false;
    if (h.type != wantType) return false;
    if (h.payloadLen > kMaxPayload || h.payloadLen > len) return false;
    if (h.payloadLen > 0) {
        if (!PipeTransfer(pipe, payload, h.payloadLen, false, timeoutMs, completed) || !completed)
            return false;
    }
    len = h.payloadLen;
    return true;
}

} // namespace iris::usnbroker
