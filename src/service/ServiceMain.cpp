// IrisService —— USN 句柄经纪服务（无状态；设计依据 doc/service-design.md §5）
//
// 运行形态：
//   （无参数）SCM 服务：由服务控制管理器启动，ServiceMain → 管道服务循环
//   --console   前台调试：控制台运行同一管道服务（开发/集成测试用）
//   --install   （须提权）注册服务并启动；幂等（已存在则复用并确保运行）
//               DEMAND_START + 授予交互用户启动权；授权失败回退自启+延迟
//   --uninstall （须提权）停止并删除服务
//
// 安全模型（§5.3，三层缺一不可）：
//   1. 管道 DACL：仅 SYSTEM / Administrators / 交互用户可连接
//   2. 客户端校验：同目录 Iris.exe（路径绑定部署边界），失败记日志并断开
//   3. 动作白名单：唯一特权动作 OpenVolume，盘符单字符白名单，路径由服务端拼接
//
// 无状态承诺：不保存游标、不缓存句柄（复制给客户端后立即关闭本地引用）、
// 不写任何文件——服务停止/重启不影响已发出的句柄（所有权归客户端）。
#include "service/PipeIo.h"
#include "service/ServiceProtocol.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#include <winioctl.h>  // USN_JOURNAL_DATA / FSCTL_QUERY_USN_JOURNAL

namespace iris::usnbroker {

namespace {

constexpr wchar_t kServiceDisplayName[] = L"Iris USN Broker Service";
constexpr wchar_t kServiceDesc[] =
    L"为 Iris 提供非提权卷句柄经纪（MFT 枚举/USN 读取）。无状态，仅响应本机 Iris.exe。";

// 管道 DACL：SYSTEM / Administrators / 交互用户 完全访问；拒绝远程连接由
// PIPE_REJECT_REMOTE_CLIENTS 另行保证
constexpr wchar_t kPipeSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)";

// 服务 DACL：保留 SYSTEM/BA 完整管理权，交互用户授予查询+启动（RP）
constexpr wchar_t kServiceSddl[] =
    L"D:P(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRRC;;;BA)(A;;CCLCSWRP;;;IU)";

constexpr uint32_t kFrameTimeoutMs = 15000;  // 单帧收发上限（正常客户端毫秒级到达）

void SvcLog(const wchar_t* msg) {
    OutputDebugStringW((std::wstring(L"[IrisService] ") + msg).c_str());
    std::wprintf(L"[IrisService] %ls\n", msg);
}

// 裸句柄 RAII
struct HandleGuard {
    HANDLE h;
    ~HandleGuard() { if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

// ============================================================================
// 单连接处理
// ============================================================================

// 打开客户端进程并校验（同目录 Iris.exe）。成功时填充 clientProc
// （PROCESS_DUP_HANDLE，会话期间保持，OpenVolume 复制句柄用）。
bool OpenAndValidateClient(HANDLE pipe, HANDLE& clientProc, std::wstring& image) {
    clientProc = nullptr;
    DWORD pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return false;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE,
                              FALSE, pid);
    if (!proc) return false;

    wchar_t path[MAX_PATH * 2] = {};
    DWORD size = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(proc, 0, path, &size)) {
        CloseHandle(proc);
        return false;
    }
    wchar_t self[MAX_PATH * 2] = {};
    DWORD selfSize = ARRAYSIZE(self);
    GetModuleFileNameW(nullptr, self, selfSize);

    if (!IsAllowedClientPath(path, self)) {
        // 入侵尝试信号：记下 PID 与镜像路径
        SvcLog((std::wstring(L"拒绝未授权客户端 pid=") + std::to_wstring(pid) +
                L" path=" + path).c_str());
        CloseHandle(proc);
        return false;
    }
    image = path;
    clientProc = proc;
    return true;
}

// 唯一特权动作：以本服务（SYSTEM）身份打开卷设备并复制给客户端。
OpenVolumeReplyPayload HandleOpenVolume(HANDLE clientProc, const OpenVolumePayload& req) {
    if (!IsValidDriveLetter(req.driveLetter))
        return {static_cast<uint32_t>(OpenVolumeResult::Rejected), 0, 0};

    const std::wstring volPath =
        L"\\\\.\\" + std::wstring(1, static_cast<wchar_t>(req.driveLetter)) + L":";
    HANDLE vol = CreateFileW(volPath.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (vol == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        SvcLog((std::wstring(L"开卷失败 ") + volPath + L" err=" +
                std::to_wstring(err)).c_str());
        return {static_cast<uint32_t>(OpenVolumeResult::OpenFailed), err, 0};
    }

    // journal 存在性预检（让"无 USN journal"也能结构化报错，而非客户端枚举时才炸）
    USN_JOURNAL_DATA jd{};
    DWORD br = 0;
    if (!DeviceIoControl(vol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                         &jd, sizeof(jd), &br, nullptr)) {
        const DWORD err = GetLastError();
        CloseHandle(vol);
        return {static_cast<uint32_t>(OpenVolumeResult::OpenFailed), err, 0};
    }

    // 复制句柄到客户端进程：复制完成后本地引用立即关闭（无状态承诺）
    HANDLE dup = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), vol, clientProc, &dup,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
        const DWORD err = GetLastError();
        CloseHandle(vol);
        return {static_cast<uint32_t>(OpenVolumeResult::OpenFailed), err, 0};
    }
    CloseHandle(vol);
    SvcLog((std::wstring(L"已交付卷句柄 ") + volPath).c_str());
    return {static_cast<uint32_t>(OpenVolumeResult::Ok), 0,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dup))};
}

// 单会话：校验客户端 → 消息循环（协议违规/超时/未知类型一律断开）
void HandleConnection(HANDLE pipe) {
    HANDLE clientProc = nullptr;
    std::wstring image;
    if (!OpenAndValidateClient(pipe, clientProc, image)) {
        // 尽力回一个结构化拒绝（方便合法但配置错误的场景诊断），随后断开
        const OpenVolumeReplyPayload rej{
            static_cast<uint32_t>(OpenVolumeResult::Rejected), 0, 0};
        PipeSendFrame(pipe, static_cast<uint16_t>(MsgType::OpenVolumeReply),
                      &rej, sizeof(rej), 1000);
        return;
    }
    HandleGuard procGuard{clientProc};

    while (true) {
        uint8_t header[kFrameHeaderSize];
        bool completed = false;
        if (!PipeTransfer(pipe, header, kFrameHeaderSize, false,
                          kFrameTimeoutMs, completed) || !completed)
            return;
        FrameHeader h;
        if (ParseFrameHeader(header, kFrameHeaderSize, h) != ParseResult::Ok) return;

        uint8_t payload[kMaxPayload];
        if (h.payloadLen > 0) {
            if (!PipeTransfer(pipe, payload, h.payloadLen, false,
                              kFrameTimeoutMs, completed) || !completed)
                return;
        }

        switch (static_cast<MsgType>(h.type)) {
        case MsgType::Ping: {
            if (h.payloadLen != sizeof(PingPayload)) return;
            const PongPayload pong{kProtoVer, kServiceVersion};
            if (!PipeSendFrame(pipe, static_cast<uint16_t>(MsgType::Pong),
                               &pong, sizeof(pong), 5000))
                return;
            break;
        }
        case MsgType::OpenVolume: {
            if (h.payloadLen != sizeof(OpenVolumePayload)) return;
            OpenVolumePayload req{};
            std::memcpy(&req, payload, sizeof(req));
            const OpenVolumeReplyPayload reply = HandleOpenVolume(clientProc, req);
            if (!PipeSendFrame(pipe, static_cast<uint16_t>(MsgType::OpenVolumeReply),
                               &reply, sizeof(reply), 5000))
                return;
            break;
        }
        default:
            return;  // 未知类型 → 断开（协议纪律，不做探索面）
        }
    }
}

// ============================================================================
// 管道服务循环（连接为同步逐个处理：M1 单客户端；M2 起再评估多实例并发）
// ============================================================================

HANDLE g_stopEvent = nullptr;

DWORD RunPipeServer(HANDLE stopEvent) {
    bool firstInstance = true;
    while (true) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = FALSE;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kPipeSddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr))
            return GetLastError();

        HANDLE pipe = CreateNamedPipeW(
            kPipePath,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),  // 防名称抢占
            PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
            4, 4096, 4096, 0, &sa);
        LocalFree(sa.lpSecurityDescriptor);
        if (pipe == INVALID_HANDLE_VALUE) return GetLastError();
        firstInstance = false;

        // 等连接或停止（重叠 ConnectNamedPipe + 双事件等待 → 服务可及时停）
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            CloseHandle(pipe);
            return GetLastError();
        }
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        if (!connected) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {stopEvent, ov.hEvent};
                const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w != WAIT_OBJECT_0 + 1) {  // 停止（或等待失败）→ 取消并退出
                    CancelIoEx(pipe, &ov);
                    CloseHandle(ov.hEvent);
                    CloseHandle(pipe);
                    return 0;
                }
                DWORD transferred = 0;
                connected = GetOverlappedResult(pipe, &ov, &transferred, FALSE);
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;  // 客户端在 Create 与 Connect 之间连上
            }
        }
        CloseHandle(ov.hEvent);

        if (connected) HandleConnection(pipe);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

// ============================================================================
// SCM 骨架
// ============================================================================

SERVICE_STATUS        g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;

void ReportStatus(DWORD state, DWORD waitHint = 0) {
    if (!g_statusHandle) return;
    g_status.dwCurrentState = state;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted =
        (state == SERVICE_RUNNING)
            ? static_cast<DWORD>(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN)
            : 0;
    SetServiceStatus(g_statusHandle, &g_status);
}

DWORD WINAPI CtrlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportStatus(SERVICE_STOP_PENDING, 3000);
        if (g_stopEvent) SetEvent(g_stopEvent);
        return NO_ERROR;
    default:
        return NO_ERROR;
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, CtrlHandler, nullptr);
    if (!g_statusHandle) return;
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ReportStatus(SERVICE_START_PENDING, 1000);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) { ReportStatus(SERVICE_STOPPED); return; }
    ReportStatus(SERVICE_RUNNING);

    const DWORD err = RunPipeServer(g_stopEvent);
    if (err != 0)
        SvcLog((std::wstring(L"管道服务异常退出 err=") + std::to_wstring(err)).c_str());
    ReportStatus(SERVICE_STOPPED);
}

int RunAsService() {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(kServiceName), ServiceMain},
        {nullptr, nullptr},
    };
    return StartServiceCtrlDispatcherW(table) ? 0 : static_cast<int>(GetLastError());
}

// ============================================================================
// --install / --uninstall（须提权运行）
// ============================================================================

// 授予交互用户启动权；失败返回 false（调用方回退 AUTO_START+延迟）
bool GrantUserStartRights(SC_HANDLE svc) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kServiceSddl, SDDL_REVISION_1, &sd, nullptr))
        return false;
    const BOOL ok = SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    if (!ok) SvcLog(L"SetServiceObjectSecurity 失败");
    return ok != FALSE;
}

int CmdInstall() {
    wchar_t self[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        SvcLog(L"打开 SCM 失败（需要管理员权限）");
        return static_cast<int>(GetLastError());
    }
    HandleGuard scmGuard{scm};

    SC_HANDLE svc = CreateServiceW(scm, kServiceName, kServiceDisplayName,
                                   SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                   SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                   self, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS)
        svc = OpenServiceW(scm, kServiceName,
                           SERVICE_START | SERVICE_CHANGE_CONFIG | READ_CONTROL | WRITE_DAC);
    if (!svc) {
        SvcLog(L"创建/打开服务失败");
        return static_cast<int>(GetLastError());
    }
    HandleGuard svcGuard{svc};

    // 交互用户启动权；受阻（域策略等）→ 回退自启+延迟（Iris 只连不拉）
    if (!GrantUserStartRights(svc)) {
        if (!ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START,
                                  SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, kServiceDisplayName))
            SvcLog(L"回退 AUTO_START 失败");
        SERVICE_DELAYED_AUTO_START_INFO delay{TRUE};
        ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delay);
    }

    SERVICE_STATUS st{};
    if (!QueryServiceStatus(svc, &st) || st.dwCurrentState != SERVICE_RUNNING)
        StartServiceW(svc, 0, nullptr);
    SvcLog(L"安装完成");
    return 0;
}

int CmdUninstall() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return static_cast<int>(GetLastError());
    HandleGuard scmGuard{scm};
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE);
    if (!svc) {
        SvcLog(L"服务不存在或无权访问");
        return static_cast<int>(GetLastError());
    }
    HandleGuard svcGuard{svc};
    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);  // 未运行则忽略
    if (!DeleteService(svc)) return static_cast<int>(GetLastError());
    SvcLog(L"已卸载");
    return 0;
}

// ============================================================================
// 入口
// ============================================================================

int RunConsole() {
    std::wprintf(L"IrisService console 模式（Ctrl+C 退出）\n");
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) return 1;
    SetConsoleCtrlHandler(
        [](DWORD type) -> BOOL {
            if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
                if (g_stopEvent) SetEvent(g_stopEvent);
                return TRUE;
            }
            return FALSE;
        }, TRUE);
    return static_cast<int>(RunPipeServer(g_stopEvent));
}

} // namespace

} // namespace iris::usnbroker

int wmain(int argc, wchar_t** argv) {
    using namespace iris::usnbroker;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--install") == 0)   return CmdInstall();
        if (wcscmp(argv[i], L"--uninstall") == 0) return CmdUninstall();
        if (wcscmp(argv[i], L"--console") == 0)   return RunConsole();
    }
    return RunAsService();
}
