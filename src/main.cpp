// Iris 程序入口 —— 【临时】UI 预览脚手架
//
// ⚠️ 这是为肉眼验证 SearchWindow 重新设计效果而放的临时 mock：注入 15 条样本数据，
//    搜索框输入即按 标题/副标题 子串过滤（不区分大小写）。真正的入口在 P2 的
//    app-entry 模块实现（接线 FileProvider→SearchEngine→SearchWindow），届时替换本文件。
//
// 说明：Qt5 静态链接所需的静态插件由 CMake 的 qt5_import_plugins(Iris) 自动生成
//       Q_IMPORT_PLUGIN 并链接，因此此处无需手写 Q_IMPORT_PLUGIN（手写会造成重复符号）。

#include <cwctype>
#include <string>

#include <QApplication>
#include <QObject>
#include <QMessageBox>
#include <QString>
#include <QVector>
#include <QWinEventNotifier>

#include "provider/IProvider.h"
#include "ui/SearchWindow.h"
#include "ui/TrayIcon.h"
#include "ui/HotkeyManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using iris::ItemType;
using iris::ResultItem;
using iris::SearchWindow;
using iris::TrayIcon;
using iris::HotkeyManager;

namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    return s;
}

bool ContainsCI(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ToLower(hay).find(ToLower(needle)) != std::wstring::npos;
}

QVector<ResultItem> MakeMockData() {
    QVector<ResultItem> v;
    const auto add = [&](const wchar_t* title, const wchar_t* sub, ItemType t) {
        ResultItem it;
        it.title    = title;
        it.subtitle = sub;
        it.path     = sub;
        it.type     = t;
        it.pathDepth = 3;
        v.push_back(it);
    };
    add(L"记事本",                    L"C:\\Windows\\System32\\notepad.exe", ItemType::APPLICATION);
    add(L"文件资源管理器",            L"C:\\Windows\\explorer.exe", ItemType::APPLICATION);
    add(L"命令提示符",                L"C:\\Windows\\System32\\cmd.exe", ItemType::APPLICATION);
    add(L"注册表编辑器",              L"C:\\Windows\\System32\\regedit.exe", ItemType::APPLICATION);
    add(L"任务管理器",                L"C:\\Windows\\System32\\taskmgr.exe", ItemType::APPLICATION);
    add(L"画图",                      L"C:\\Windows\\System32\\mspaint.exe", ItemType::APPLICATION);
    add(L"控制面板",                  L"C:\\Windows\\System32\\control.exe", ItemType::APPLICATION);
    add(L"写字板",                    L"C:\\Windows\\System32\\write.exe", ItemType::APPLICATION);
    add(L"放大镜",                    L"C:\\Windows\\System32\\magnify.exe", ItemType::APPLICATION);
    add(L"proposal.md",              L"D:\\Code\\Iris\\doc\\proposal.md", ItemType::FILE);
    add(L"detailed-design.md",       L"D:\\Code\\Iris\\doc\\detailed-design.md", ItemType::FILE);
    add(L"hosts",                    L"C:\\Windows\\System32\\drivers\\etc\\hosts", ItemType::FILE);
    add(L"GitHub",                   L"https://github.com", ItemType::BOOKMARK);
    add(L"必应",                     L"https://www.bing.com", ItemType::BOOKMARK);
    add(L"哔哩哔哩",                 L"https://www.bilibili.com", ItemType::BOOKMARK);
    return v;
}

} // namespace

int main(int argc, char* argv[]) {
    // ── 单实例：已有实例运行时，通知其显示窗口后立即退出，不另起进程 ──
    static constexpr const wchar_t* kMutexName = L"Local\\IrisSingleInstance";
    static constexpr const wchar_t* kShowEvent = L"Local\\IrisShowWindow";
    CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShowEvent)) {
            SetEvent(h);
            CloseHandle(h);
        }
        return 0;  // 不创建 QApplication、不显示任何窗口，直接退出
    }

    QApplication app(argc, argv);
    app.setApplicationName("Iris");
    app.setOrganizationName("IrisSearch");

    QVector<ResultItem> all = MakeMockData();
    SearchWindow w;

    // 首实例创建命名事件；后续实例启动时 SetEvent 唤醒此处以显示窗口
    HANDLE hShowEvent = CreateEventW(nullptr, FALSE, FALSE, kShowEvent);
    QWinEventNotifier showNotifier(hShowEvent);
    showNotifier.setEnabled(true);
    QObject::connect(&showNotifier, &QWinEventNotifier::activated, &w, [&w]() {
        w.showWithFadeIn();
    });

    // 全局热键 Alt+Space：toggle 显示/隐藏搜索框
    HotkeyManager hotkey;
    hotkey.Register(MOD_ALT, VK_SPACE);
    QObject::connect(&hotkey, &HotkeyManager::hotkeyPressed, &w, [&w]() {
        if (w.isVisible()) {
            w.hideWithFadeOut();
        } else {
            w.showWithFadeIn();
        }
    });
    QObject::connect(&hotkey, &HotkeyManager::hotkeyConflict, [](const QString& msg) {
        QMessageBox::warning(nullptr, QString::fromUtf8("Iris"), msg);
    });

    // 搜索：按 标题/副标题 子串（不区分大小写）过滤 mock 数据
    QObject::connect(&w, &SearchWindow::searchRequested, &w,
        [&w, &all](const QString& q) {
            const std::wstring needle = q.toStdWString();
            QVector<ResultItem> out;
            if (!needle.empty()) {  // 输入框为空时不显示列表
                for (const ResultItem& it : all) {
                    if (ContainsCI(it.title, needle) || ContainsCI(it.subtitle, needle))
                        out.push_back(it);
                }
            }
            w.onSearchFinished(out);
        });

    // 预览模式不真正打开文件（避免副作用）；真正打开逻辑在 app-entry 实现
    QObject::connect(&w, &SearchWindow::itemActivated,
        [](const ResultItem&) {});

    // 系统托盘：后台运行入口 + 右键菜单（搜索/重新索引/设置/关于/退出）
    TrayIcon tray;
    const bool trayOk = tray.Create();
    QObject::connect(&tray, &TrayIcon::searchRequested, &w, [&w]() {
        w.showWithFadeIn();
    });
    QObject::connect(&tray, &TrayIcon::reindexRequested, &tray, [&tray]() {
        // 真正入口应接 FileProvider::Refresh()；此处 mock 仅提示
        tray.ShowNotification(QString::fromUtf8("Iris"),
                              QString::fromUtf8("重新索引完成"));
    });
    QObject::connect(&tray, &TrayIcon::settingsRequested, []() {
        QMessageBox::information(nullptr, QString::fromUtf8("Iris 设置"),
            QString::fromUtf8("设置功能将在后续版本提供。"));
    });
    QObject::connect(&tray, &TrayIcon::aboutRequested, []() {
        QMessageBox::information(nullptr, QString::fromUtf8("关于 Iris"),
            QString::fromUtf8("Iris v1.0.0\n快速本地搜索工具\n\n"
                              "像 Everything 一样快，像 Alfred 一样优雅。"));
    });
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QApplication::quit);

    w.showWithFadeIn();  // 输入框为空 → 列表不显示（仅输入框）

    // 后台运行：窗口隐藏（Esc/失焦淡出）不再退出进程，仅托盘“退出”结束。
    if (!trayOk) {
        QMessageBox::warning(nullptr, QString::fromUtf8("Iris"),
            QString::fromUtf8("系统托盘不可用，窗口关闭后进程将驻留。"));
    }

    return app.exec();
}
