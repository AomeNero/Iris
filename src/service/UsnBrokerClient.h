// Iris Service —— 句柄经纪客户端（Iris.exe 侧；纯 Win32，无 Qt）
// 设计依据: doc/service-design.md §5.5
// 契约：连接服务 → 发请求 → 限时等回复。任何失败返回 false（超时/管道不存在/
// 服务拒绝/开卷失败），win32Err 可选带回底层错误码。OpenVolume 成功时 out 为
// 本进程可直接使用的卷句柄（DuplicateHandle 产物，所有权归调用方，INVALID_HANDLE_VALUE
// 表示失败）。
#pragma once

#include <cstdint>

struct HANDLE__;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace iris::usnbroker {

/// 探活：服务可达返回 true 并带回协议/服务版本（诊断与安装确认用）。
bool Ping(uint32_t& protoVer, uint32_t& serviceVersion, uint32_t timeoutMs = 2000);

/// 请求打开卷设备句柄（如 L'C' → \\.\C:）。失败时 out = INVALID_HANDLE_VALUE。
bool OpenVolume(wchar_t driveLetter, HANDLE& out, uint32_t* win32Err = nullptr,
                uint32_t timeoutMs = 2000);

} // namespace iris::usnbroker
