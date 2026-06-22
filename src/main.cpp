// Iris 程序入口
//
// 接通真实搜索链路：FileProvider + BookmarkProvider + AppProvider →
// SearchEngine → SearchWindow。系统托盘后台运行、Alt+Space 全局热键、单实例。
//
// 说明：Qt5 静态插件由 CMake 的 qt5_import_plugins(Iris) 自动生成 Q_IMPORT_PLUGIN
//       并链接，此处无需手写（手写会造成重复符号）。

#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

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
#include "core/Config.h"
#include "core/HotkeySpec.h"
#include "core/HistoryStore.h"
#include "core/Logger.h"
#include "core/WinUtil.h"
#include "ui/SearchWindow.h"
#include "ui/TrayIcon.h"
#include "ui/HotkeyManager.h"
#include "ui/Theme.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>  // ShellExecuteW

using iris::ResultItem;
using iris::SearchWindow;
using iris::SearchEngine;
using iris::Config;
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
    // ── 解析 --minimized：开机自启时静默启动（仅托盘，不弹搜索窗）──
    const bool minimized = [&] {
        for (int i = 1; i < argc; ++i)
            if (std::string_view(argv[i]) == "--minimized") return true;
        return false;
    }();

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
    app.setQuitOnLastWindowClosed(false);  // 托盘驻留：QMessageBox 等关闭不退出，仅托盘"退出"菜单退出

    // QVector<ResultItem> 跨线程 queued 传递需注册（engine 工作线程 → UI 线程）
    qRegisterMetaType<QVector<ResultItem>>("QVector<ResultItem>");

    // ── 数据便携化：数据库 Iris.db 与日志 log/ 都存 Iris.exe 同级目录（便携式部署）──
    const std::filesystem::path exeDir(iris::WinUtil::GetExeDir());
    iris::Logger::Init(exeDir / L"log");   // 日志进 exe 同级 log/（Logger 内部自动建目录）
    HistoryStore history(exeDir / L"Iris.db");

    // ── 配置：读 config.json（不存在则用默认值并落盘，供托盘“设置”编辑）──
    auto cfg = Config::Instance().Load();
    if (!std::filesystem::exists(Config::Instance().GetConfigPath())) {
        Config::Instance().Save();  // 首次启动生成默认 config.json
    }
    // 开机自启：以 config 为权威源同步注册表 Run 键
    // （autoStart=true 时写入带 --minimized 的命令行，开机静默启动）
    iris::WinUtil::SetAutoStart(cfg.autoStart);

    // ── 三个数据源（按 config.providers 开关条件创建；Initialize 后台并行执行）──
    auto file     = cfg.providers.file.enabled     ? std::make_shared<FileProvider>()     : nullptr;
    auto bookmark = cfg.providers.bookmark.enabled ? std::make_shared<BookmarkProvider>() : nullptr;
    auto apps     = cfg.providers.app.enabled      ? std::make_shared<AppProvider>()      : nullptr;
    if (file) file->SetExcludeFlags(cfg.excludeHidden, cfg.excludeSystem);

    // ── 搜索引擎：注入 Provider + 历史（在后台 Initialize 启动前完成，
    //    happens-before：DoSearch 读取这些 shared_ptr 无数据竞争）──
    SearchEngine engine;
    engine.SetProviders(file, bookmark, apps);
    engine.SetHistoryStore(&history);
    engine.SetMaxResults(cfg.maxResults);
    iris::SetCurrentTheme(iris::ParseThemeName(cfg.theme));  // 主题：light/dark（启动时应用）

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
    // 热键：解析 config 里的字符串（如 "Alt+Space"）；失败回退默认 Alt+Space
    unsigned hkMods = MOD_ALT, hkVk = VK_SPACE;
    iris::HotkeySpec::Parse(cfg.hotkey, hkMods, hkVk);
    hotkey.Register(hkMods, hkVk);
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
    tray.SetAutoStartChecked(cfg.autoStart);  // “开机自启”勾选态随 config 初始化
    QObject::connect(&tray, &TrayIcon::searchRequested, &w, [&w]() {
        w.showWithFadeIn();
    });
    QObject::connect(&tray, &TrayIcon::reindexRequested, &tray,
        [&tray, &file, &bookmark, &apps]() {
            if (file)     file->Refresh();
            if (bookmark) bookmark->Refresh();
            if (apps)     apps->Refresh();
            tray.ShowNotification(QString::fromUtf8("Iris"),
                                  QString::fromUtf8("重新索引完成"));
        });
    QObject::connect(&tray, &TrayIcon::settingsRequested, []() {
        // 打开 config.json 供用户编辑（重启 Iris 后生效）
        const std::wstring p = Config::Instance().GetConfigPath().wstring();
        ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    QObject::connect(&tray, &TrayIcon::aboutRequested, [&w]() {
        w.SetSuppressAutoHide(true);   // 对话框夺焦期间阻止自动隐藏，避免模态恢复崩溃
        QMessageBox::information(nullptr, QString::fromUtf8("关于 Iris"),
            QString::fromUtf8("Iris v1.0.1\n超级启动器\n"
                              "能帮你秒开软件，还能搜文件和网络收藏夹哦！\n"
                              "Author:AomeNero eMail:yotianya@gmail.com\n\n"));
        // 对话框关闭后主动夺回焦点（Qt::Tool 窗口不会自动重新获焦，否则卡住）
        w.activateWindow();
        w.raise();
        w.setFocus(Qt::OtherFocusReason);
        w.SetSuppressAutoHide(false);
    });
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QApplication::quit);
    // 开机自启开关：即时更新 config + 注册表（config 为权威源）
    QObject::connect(&tray, &TrayIcon::autoStartToggled, [](bool on) {
        auto cur = Config::Instance().Get();
        cur.autoStart = on;
        Config::Instance().Set(cur);
        Config::Instance().Save();
        iris::WinUtil::SetAutoStart(on);
    });

    if (!minimized) {
        w.showWithFadeIn();  // 窗口先于索引显示（输入框为空 → 列表不显示，仅输入框）
    }
    // --minimized（开机自启）：仅托盘驻留，不弹搜索窗，按热键才唤出

    if (!trayOk) {
        w.SetSuppressAutoHide(true);
        QMessageBox::warning(nullptr, QString::fromUtf8("Iris"),
            QString::fromUtf8("系统托盘不可用，窗口关闭后进程将驻留。"));
        w.activateWindow();
        w.raise();
        w.setFocus(Qt::OtherFocusReason);
        w.SetSuppressAutoHide(false);
    }

    // ── 后台并行索引：窗口已显示，三个 Provider 的 Initialize 不阻塞 UI。
    //    值捕获 shared_ptr（CLAUDE.md 硬性规则：跨线程 Lambda 禁止 [&]）。
    //    DoSearch 的 IsReady() 门控保证未就绪的 Provider 被自动跳过 → 增量就绪。──
    std::vector<std::thread> initThreads;
    if (file)     initThreads.emplace_back([file]()     { file->Initialize(); });
    if (bookmark) initThreads.emplace_back([bookmark]() { bookmark->Initialize(); });
    if (apps)     initThreads.emplace_back([apps]()     { apps->Initialize(); });

    const int ret = app.exec();

    // 先 join 索引线程（确保 Initialize 已结束），再 Shutdown（避免与 Initialize 并发竞争）
    for (auto& t : initThreads) {
        if (t.joinable()) t.join();
    }

    // 有序关闭（仅关闭已启用的 Provider）
    if (apps)     apps->Shutdown();
    if (bookmark) bookmark->Shutdown();
    if (file)     file->Shutdown();
    iris::Logger::Shutdown();
    return ret;
}
