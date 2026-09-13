// Iris Service —— 句柄经纪客户端实现
#include "service/UsnBrokerClient.h"

#include "service/PipeIo.h"
#include "service/ServiceProtocol.h"

namespace iris::usnbroker {

namespace {

// 裸句柄 RAII（会话级：析构即断开）
struct PipeGuard {
    HANDLE h;
    ~PipeGuard() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

bool Connect(HANDLE& out) {
    out = CreateFileW(kPipePath, GENERIC_READ | GENERIC_WRITE, 0,
                      nullptr, OPEN_EXISTING, 0, nullptr);
    return out != INVALID_HANDLE_VALUE;
}

} // namespace

bool Ping(uint32_t& protoVerOut, uint32_t& serviceVersionOut, uint32_t timeoutMs) {
    protoVerOut = 0;
    serviceVersionOut = 0;
    HANDLE pipe;
    if (!Connect(pipe)) return false;
    PipeGuard guard{pipe};

    const PingPayload req{kProtoVer};
    if (!PipeSendFrame(pipe, static_cast<uint16_t>(MsgType::Ping),
                       &req, sizeof(req), timeoutMs))
        return false;

    PongPayload reply{};
    uint32_t len = sizeof(reply);
    if (!PipeRecvFrame(pipe, static_cast<uint16_t>(MsgType::Pong),
                       reinterpret_cast<uint8_t*>(&reply), len, timeoutMs))
        return false;
    if (len != sizeof(reply)) return false;
    protoVerOut = reply.protoVer;
    serviceVersionOut = reply.serviceVersion;
    return true;
}

bool OpenVolume(wchar_t driveLetter, HANDLE& out, uint32_t* win32Err, uint32_t timeoutMs) {
    if (win32Err) *win32Err = 0;
    out = INVALID_HANDLE_VALUE;
    if (!IsValidDriveLetter(static_cast<uint16_t>(driveLetter))) return false;

    HANDLE pipe;
    if (!Connect(pipe)) {
        if (win32Err) *win32Err = GetLastError();
        return false;
    }
    PipeGuard guard{pipe};

    const OpenVolumePayload req{static_cast<uint16_t>(driveLetter)};
    if (!PipeSendFrame(pipe, static_cast<uint16_t>(MsgType::OpenVolume),
                       &req, sizeof(req), timeoutMs))
        return false;

    OpenVolumeReplyPayload reply{};
    uint32_t len = sizeof(reply);
    if (!PipeRecvFrame(pipe, static_cast<uint16_t>(MsgType::OpenVolumeReply),
                       reinterpret_cast<uint8_t*>(&reply), len, timeoutMs))
        return false;
    if (len != sizeof(reply)) return false;
    if (reply.result != static_cast<uint32_t>(OpenVolumeResult::Ok)) {
        if (win32Err) *win32Err = reply.win32Err;
        return false;
    }
    out = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(reply.handleValue));
    return out != nullptr && out != INVALID_HANDLE_VALUE;
}

} // namespace iris::usnbroker
