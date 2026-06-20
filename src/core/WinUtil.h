// Iris Core —— Windows API 封装
// 设计依据: doc/detailed-design.md §5.5
#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>  // REFKNOWNFOLDERID, IShellLink

namespace iris::WinUtil {

/// 当前焦点窗口所在显示器的工作区矩形（用于窗口居中）
RECT GetCurrentMonitorRect();

/// 判断根路径（如 "C:\\"）是否为 NTFS 卷
bool IsNTFSVolume(const std::wstring& rootPath);

/// 所有本地固定磁盘根路径（如 C:\, D:\）
std::vector<std::wstring> GetLocalDrives();

struct ShortcutInfo {
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring iconPath;
    int          iconIndex = 0;
    std::wstring description;
};
/// 解析 .lnk 快捷方式（调用方需已 CoInitialize）。失败返回空 targetPath。
ShortcutInfo ResolveShortcut(const std::wstring& lnkPath);

/// 开机自启（写 HKCU\\...\\Run）。失败返回 false。
bool SetAutoStart(bool enable);
bool IsAutoStartEnabled();

/// 已知文件夹路径（FOLDERID_RoamingAppData 等）
std::wstring GetKnownFolderPath(REFKNOWNFOLDERID rfid);

struct UwpAppInfo {
    std::wstring name;
    std::wstring appUserModelId;
    std::wstring iconPath;
};
/// UWP 应用枚举（P2 stub：暂返回空，后续用 PackageManager COM 实现）
std::vector<UwpAppInfo> EnumerateUwpApps();

/// 确保目录存在（递归创建）
bool EnsureDirectory(const std::wstring& path);

} // namespace iris::WinUtil
