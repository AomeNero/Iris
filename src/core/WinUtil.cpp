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
const std::wstring kRunKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t*     kRunName = L"Iris";
} // namespace

bool SetAutoStart(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey.c_str(), 0, KEY_SET_VALUE, &hKey)
        != ERROR_SUCCESS)
        return false;
    bool ok = true;
    if (enable) {
        WCHAR exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const DWORD bytes = static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(WCHAR));
        ok = RegSetValueExW(hKey, kRunName, 0, REG_SZ,
                            reinterpret_cast<BYTE*>(exePath), bytes) == ERROR_SUCCESS;
    } else {
        RegDeleteValueW(hKey, kRunName);
    }
    RegCloseKey(hKey);
    return ok;
}

bool IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey.c_str(), 0, KEY_READ, &hKey)
        != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    const bool exists =
        RegQueryValueExW(hKey, kRunName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
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

std::vector<UwpAppInfo> EnumerateUwpApps() {
    return {};  // P2 stub，后续用 PackageManager COM 实现
}

bool EnsureDirectory(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

} // namespace iris::WinUtil
