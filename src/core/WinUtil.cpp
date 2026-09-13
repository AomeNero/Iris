// Iris Core —— Windows API 封装实现
#include "core/WinUtil.h"
#include "core/Logger.h"

#include <filesystem>
#include <system_error>

namespace iris::WinUtil {

RECT GetCurrentMonitorRect() {
    // 用光标位置定位当前显示器（搜索框在用户注意的区域附近弹出）
    POINT pt{0, 0};
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);
    return mi.rcWork;
}

bool IsNTFSVolume(const std::wstring& rootPath) {
    WCHAR fsName[MAX_PATH] = {};
    if (!GetVolumeInformationW(rootPath.c_str(), nullptr, 0, nullptr, nullptr,
                               nullptr, fsName, MAX_PATH))
        return false;
    return _wcsicmp(fsName, L"NTFS") == 0;
}

std::vector<std::wstring> GetLocalDrives() {
    std::vector<std::wstring> drives;
    WCHAR buf[256] = {};
    const DWORD len = GetLogicalDriveStringsW(256, buf);
    for (DWORD i = 0; i < len;) {
        const std::wstring d(&buf[i]);
        i += static_cast<DWORD>(d.size() + 1);
        if (GetDriveTypeW(d.c_str()) == DRIVE_FIXED) drives.push_back(d);
    }
    return drives;
}

ShortcutInfo ResolveShortcut(const std::wstring& lnkPath) {
    ShortcutInfo info;
    // 调用方需已 CoInitialize。
    IShellLinkW*    psl = nullptr;
    IPersistFile*   ppf = nullptr;

    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&psl))))
        return info;
    if (FAILED(psl->QueryInterface(IID_PPV_ARGS(&ppf)))) { psl->Release(); return info; }
    if (FAILED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
        ppf->Release(); psl->Release(); return info;
    }

    WCHAR target[MAX_PATH] = {};
    WCHAR args[MAX_PATH]   = {};
    WCHAR workdir[MAX_PATH] = {};
    WCHAR desc[MAX_PATH]   = {};
    WCHAR iconPath[MAX_PATH] = {};

    if (SUCCEEDED(psl->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)))
        info.targetPath = target;
    psl->GetArguments(args, MAX_PATH);             info.arguments = args;
    psl->GetWorkingDirectory(workdir, MAX_PATH);   info.workingDir = workdir;
    psl->GetDescription(desc, MAX_PATH);           info.description = desc;
    int iconIdx = 0;
    psl->GetIconLocation(iconPath, MAX_PATH, &iconIdx);
    info.iconPath  = iconPath;
    info.iconIndex = iconIdx;

    ppf->Release();
    psl->Release();
    return info;
}

namespace {
const std::wstring kRunKey    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t*     kRunName   = L"Iris";
const wchar_t*     kTaskName  = L"Iris";  // 任务计划程序任务名（提权自启用）

// 运行 schtasks 子命令，返回退出码（-1 = 启动失败）。
// GUI 进程无控制台：CREATE_NO_WINDOW 防止闪窗。
int RunSchtasks(const std::wstring& args) {
    std::wstring cmd = L"schtasks.exe " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 8000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
} // namespace

bool SetAutoStart(bool enable) {
    // requireAdministrator 清单下 HKCU Run 键自启会被 Windows 静默跳过——
    // 自启走任务计划程序（登录触发 + 最高权限运行；创建一次后开机无 UAC 弹窗）。
    // 无论开关先清理旧 Run 键值（老版本迁移）；任务不存在时 /Delete 失败被忽略。
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey.c_str(), 0, KEY_SET_VALUE,
                          &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, kRunName);
            RegCloseKey(hKey);
        }
    }
    if (!enable) {
        RunSchtasks(std::wstring(L"/Delete /TN \"") + kTaskName + L"\" /F");
        return true;
    }
    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // /TR 参数内嵌引号转义：任务动作 = "exe路径" --minimized（开机静默仅托盘）
    const std::wstring args =
        std::wstring(L"/Create /F /TN \"") + kTaskName +
        L"\" /SC ONLOGON /RL HIGHEST /TR \"\\\"" + exePath + L"\\\" --minimized\"";
    return RunSchtasks(args) == 0;
}

bool IsAutoStartEnabled() {
    return RunSchtasks(std::wstring(L"/Query /TN \"") + kTaskName + L"\"") == 0;
}

std::wstring GetKnownFolderPath(REFKNOWNFOLDERID rfid) {
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &p))) {
        result = p;
        CoTaskMemFree(p);
    }
    return result;
}

std::wstring GetExeDir() {
    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(exePath).parent_path().wstring();
}

std::vector<UwpAppInfo> EnumerateUwpApps() {
    return {};  // P2 stub，后续用 PackageManager COM 实现
}

bool EnsureDirectory(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

void RevealInExplorer(const std::wstring& path) {
    // explorer /select,"path"：打开父目录并选中该文件（path 含空格靠引号保护）
    const std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
}

void ShowProperties(const std::wstring& path) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;  // 使 lpVerb="properties" 生效
    sei.lpVerb = L"properties";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

} // namespace iris::WinUtil
