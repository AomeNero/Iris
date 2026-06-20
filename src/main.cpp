// Iris 程序入口
//
// 接通真实搜索链路：FileProvider + BookmarkProvider + AppProvider →
// SearchEngine → SearchWindow。系统托盘后台运行、Alt+Space 全局热键、单实例。
//
// 说明：Qt5 静态插件由 CMake 的 qt5_import_plugins(Iris) 自动生成 Q_IMPORT_PLUGIN
//       并链接，此处无需手写（手写会造成重复符号）。

#include <filesystem>
#include <memory>

#include <QApplication>
#include <QObject>
#include <QMessageBox>
#include <QString>
#include <QVector>
#include <QWinEventNotifier>

#include "provider/FileProvider.h"
#include "provider/BookmarkProvider.h"
#include "provider/AppProvider.h"
#include "engine/SearchEngine.h"
#include "core/HistoryStore.h"
#include "core/Logger.h"
#include "core/WinUtil.h"
#include "ui/SearchWindow.h"
#include "ui/TrayIcon.h"
#include "ui/HotkeyManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>  // ShellExecuteW

using iris::ResultItem;
using iris::SearchWindow;
using iris::SearchEngine;
using iris::HistoryStore;
using iris::FileProvider;
using iris::BookmarkProvider;
using iris::AppProvider;
using iris::TrayIcon;
using iris::HotkeyManager;

namespace {

// 打开结果项：文件/应用/URL 统一交系统默认处理器
void OpenItem(const ResultItem& item) {
    if (item.path.empty()) return;
    ShellExecuteW(nullptr, L"open", item.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
        return 0;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Iris");
    app.setOrganizationName("IrisSearch");

    // QVector<ResultItem> 跨线程 queued 传递需注册（engine 工作线程 → UI 线程）
    qRegisterMetaType<QVector<ResultItem>>("QVector<ResultItem>");

    // ── 数据便携化：数据库 Iris.db 与日志 log/ 都存 Iris.exe 同级目录（便携式部署）──
    const std::filesystem::path exeDir(iris::WinUtil::GetExeDir());
    iris::Logger::Init(exeDir / L"log");   // 日志进 exe 同级 log/（Logger 内部自动建目录）
    HistoryStore history(exeDir / L"Iris.db");

    // ── 三个数据源（同步初始化；FileProvider 全盘 USN 扫描可能耗时数秒）──
    auto file     = std::make_shared<FileProvider>();
    auto bookmark = std::make_shared<BookmarkProvider>();
    auto apps     = std::make_shared<AppProvider>();
    file->Initialize();
    bookmark->Initialize();
    apps->Initialize();

    // ── 搜索引擎：注入 Provider + 历史 ──
    SearchEngine engine;
    engine.SetProviders(file, bookmark, apps);
    engine.SetHistoryStore(&history);

    SearchWindow w;

    // 首实例创建命名事件；后续实例启动时 SetEvent 唤醒此处显示窗口
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
        if (w.isVisible()) w.hideWithFadeOut();
        else               w.showWithFadeIn();
    });
    QObject::connect(&hotkey, &HotkeyManager::hotkeyConflict, [](const QString& msg) {
        QMessageBox::warning(nullptr, QString::fromUtf8("Iris"), msg);
    });

    // 搜索：输入 → 异步搜索
    QObject::connect(&w, &SearchWindow::searchRequested, &w,
        [&engine](const QString& q) {
            engine.SearchAsync(q.toStdWString());
        });

    // 搜索完成 → 更新 UI（跨线程，QueuedConnection 确保 UI 线程）
    QObject::connect(&engine, &SearchEngine::searchFinished, &w,
        &SearchWindow::onSearchFinished, Qt::QueuedConnection);

    // 打开结果：系统默认处理器 + 记录历史（窗口已由 SearchWindow 在 emit 后隐藏）
    QObject::connect(&w, &SearchWindow::itemActivated,
        [&history](const ResultItem& item) {
            OpenItem(item);
            history.RecordOpen(item);
        });

    // 系统托盘：后台运行入口 + 右键菜单
    TrayIcon tray;
    const bool trayOk = tray.Create();
    QObject::connect(&tray, &TrayIcon::searchRequested, &w, [&w]() {
        w.showWithFadeIn();
    });
    QObject::connect(&tray, &TrayIcon::reindexRequested, &tray,
        [&tray, &file, &bookmark, &apps]() {
            file->Refresh();
            bookmark->Refresh();
            apps->Refresh();
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

    if (!trayOk) {
        QMessageBox::warning(nullptr, QString::fromUtf8("Iris"),
            QString::fromUtf8("系统托盘不可用，窗口关闭后进程将驻留。"));
    }

    const int ret = app.exec();

    // 有序关闭
    apps->Shutdown();
    bookmark->Shutdown();
    file->Shutdown();
    iris::Logger::Shutdown();
    return ret;
}
