// Iris UI —— 系统托盘图标实现
#include "ui/TrayIcon.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QSignalBlocker>

namespace iris {

TrayIcon::TrayIcon(QObject* parent) : QObject(parent) {}

TrayIcon::~TrayIcon() {
    delete contextMenu_;  // trayIcon_ 作为 this 的 QObject 子对象自动释放
}

bool TrayIcon::Create() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;  // 系统当前不提供托盘（如无 Explorer 外壳）
    }

    trayIcon_ = new QSystemTrayIcon(QIcon(":/iris.ico"), this);
    trayIcon_->setToolTip(QString::fromUtf8("Iris"));

    BuildMenu();
    trayIcon_->setContextMenu(contextMenu_);

    // 双击托盘图标 → 弹出搜索窗口
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    emit searchRequested();
                }
            });

    trayIcon_->show();
    return true;
}

void TrayIcon::BuildMenu() {
    contextMenu_ = new QMenu();

    QAction* aSearch   = contextMenu_->addAction(QString::fromUtf8("搜索"));
    QAction* aReindex  = contextMenu_->addAction(QString::fromUtf8("重新索引"));
    aAutoStart_ = contextMenu_->addAction(QString::fromUtf8("开机自启"));
    aAutoStart_->setCheckable(true);
    QAction* aSettings = contextMenu_->addAction(QString::fromUtf8("设置"));
    QAction* aAbout    = contextMenu_->addAction(QString::fromUtf8("关于"));
    QAction* aRestartAdmin = contextMenu_->addAction(QString::fromUtf8("以管理员重启"));
    contextMenu_->addSeparator();
    QAction* aQuit     = contextMenu_->addAction(QString::fromUtf8("退出"));

    connect(aSearch,     &QAction::triggered, this, [this] { emit searchRequested(); });
    connect(aReindex,    &QAction::triggered, this, [this] { emit reindexRequested(); });
    connect(aAutoStart_, &QAction::toggled,   this, [this](bool on) { emit autoStartToggled(on); });
    connect(aSettings,   &QAction::triggered, this, [this] { emit settingsRequested(); });
    connect(aAbout,      &QAction::triggered, this, [this] { emit aboutRequested(); });
    connect(aRestartAdmin, &QAction::triggered, this, [this] { emit restartAsAdminRequested(); });
    connect(aQuit,       &QAction::triggered, this, [this] { emit quitRequested(); });
}

void TrayIcon::SetAutoStartChecked(bool checked) {
    if (!aAutoStart_) return;
    QSignalBlocker blocker(aAutoStart_);  // 抑制 setChecked 触发的 toggled，避免误写注册表
    aAutoStart_->setChecked(checked);
}

void TrayIcon::ShowNotification(const QString& title, const QString& message) {
    if (trayIcon_) {
        trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void TrayIcon::ShowWarningNotification(const QString& title, const QString& message) {
    if (trayIcon_) {
        trayIcon_->showMessage(title, message, QSystemTrayIcon::Warning, 5000);
    }
}

void TrayIcon::SetUsnUnavailable(bool unavailable) {
    usnUnavailable_ = unavailable;
    if (!trayIcon_) return;
    trayIcon_->setToolTip(usnUnavailable_
        ? QString::fromUtf8("Iris — 实时更新不可用：索引为启动时快照\n（右键 → 以管理员重启可恢复）")
        : QString::fromUtf8("Iris"));
}

} // namespace iris
