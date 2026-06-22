// Iris UI —— 系统托盘图标（后台运行入口 + 右键菜单）
// 设计依据: doc/detailed-design.md §8.10, doc/tasks/tray-icon.md
#pragma once

#include <QObject>

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace iris {

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject* parent = nullptr);
    ~TrayIcon() override;

    // 创建托盘图标与菜单；系统不支持托盘时返回 false。
    bool Create();

    // 弹出气泡通知（索引完成、热键冲突提醒等）。
    void ShowNotification(const QString& title, const QString& message);

    // 设置"开机自启"菜单项的勾选态（初始化用；内部抑制 toggled，避免误触发写注册表）。
    void SetAutoStartChecked(bool checked);

signals:
    void searchRequested();    // "搜索" 或 双击图标
    void reindexRequested();   // "重新索引"
    void settingsRequested();  // "设置"
    void aboutRequested();     // "关于"
    void quitRequested();      // "退出"
    void autoStartToggled(bool enabled);  // "开机自启"勾选项切换

private:
    void BuildMenu();

    QSystemTrayIcon* trayIcon_   = nullptr;  // parent=this，自动释放
    QMenu*           contextMenu_ = nullptr;  // 无 QObject 父，析构手动 delete
    QAction*         aAutoStart_  = nullptr;  // 可勾选"开机自启"菜单项
};

} // namespace iris
