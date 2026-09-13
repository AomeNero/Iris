// Iris Service —— 句柄经纪协议 v1（纯编解码与校验，无 I/O，可单测）
// 设计依据: doc/service-design.md §5.2
// 帧格式（小端，本机字节序；服务与客户端同机）:
//   偏移  大小  字段
//   0     4   magic      = 'IRSB'
//   4     2   protoVer   = 1
//   6     2   type
//   8     4   payloadLen（≤ kMaxPayload）
//   12    N   payload
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iris::usnbroker {

inline constexpr uint32_t kFrameMagic      = 0x42535249u;  // 'IRSB'（小端字节序）
inline constexpr uint16_t kProtoVer        = 1;
inline constexpr uint32_t kServiceVersion  = 1;
inline constexpr size_t   kFrameHeaderSize = 12;
inline constexpr uint32_t kMaxPayload      = 4096;  // 合法载荷上限；超出即拒绝

// 管道端点（协议大版本内嵌路径，破坏性变更换路径隔离旧客户端）
inline constexpr wchar_t kPipePath[] = L"\\\\.\\pipe\\iris-usn-broker\\v1";

// SCM 服务名（--install/--uninstall 与安装确认查询共用）
inline constexpr wchar_t kServiceName[] = L"IrisService";

enum class MsgType : uint16_t {
    Ping            = 1,  // C→S  PingPayload
    Pong            = 2,  // S→C  PongPayload
    OpenVolume      = 3,  // C→S  OpenVolumePayload
    OpenVolumeReply = 4,  // S→C  OpenVolumeReplyPayload
};

enum class OpenVolumeResult : uint32_t {
    Ok           = 0,
    Rejected     = 1,  // 校验失败（非法盘符/客户端不被允许）
    OpenFailed   = 2,  // 开卷失败（win32Err 携带原因）
    ShuttingDown = 3,  // 服务停止中
};

#pragma pack(push, 1)
struct PingPayload            { uint32_t clientProtoVer; };
struct PongPayload            { uint32_t protoVer; uint32_t serviceVersion; };
struct OpenVolumePayload      { uint16_t driveLetter; };
struct OpenVolumeReplyPayload { uint32_t result; uint32_t win32Err; uint64_t handleValue; };
#pragma pack(pop)

struct FrameHeader {
    uint32_t magic      = 0;
    uint16_t protoVer   = 0;
    uint16_t type       = 0;
    uint32_t payloadLen = 0;
};

enum class ParseResult {
    Ok,        // 头部完整且合法
    NeedMore,  // 数据不足（调用方继续收）
    BadMagic,
    BadVersion,  // 0 或大于本端支持的版本
    BadLength,   // payloadLen 超限
};

/// 解析一帧头部。
ParseResult ParseFrameHeader(const uint8_t* data, size_t avail, FrameHeader& out);

/// 编码一整帧（头 + 载荷）。payloadLen > kMaxPayload 返回空向量。
std::vector<uint8_t> EncodeFrame(uint16_t type, const void* payload, uint32_t payloadLen);

/// 动作白名单：仅接受单个 ASCII 盘符（A–Z / a–z）。
bool IsValidDriveLetter(uint16_t ch);

/// 客户端校验：可执行文件须与服务同目录且名为 Iris.exe（路径绑定部署边界，
/// 便携式构建无签名；比较大小写不敏感，符合 Windows 路径语义）。
bool IsAllowedClientPath(const std::wstring& clientExePath, const std::wstring& serviceExePath);

} // namespace iris::usnbroker
