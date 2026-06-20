// Iris UI —— 全局热键管理（注册 + WM_HOTKEY 捕获合一）
// 设计依据: doc/detailed-design.md §8.9, doc/tasks/hotkey.md (T001-T004 合并)
#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>

namespace iris {

class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    // 注册全局热键；失败（如已被占用）返回 false 并 emit hotkeyConflict。
    bool Register(unsigned modifiers, unsigned vkCode);
    void Unregister();

protected:
    // 捕获 WM_HOTKEY（RegisterHotKey(nullptr,...) 投递到主线程消息队列）。
    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;

signals:
    void hotkeyPressed();
    void hotkeyConflict(const QString& message);

private:
    static constexpr int kHotkeyId = 0x49524953;  // "IRIS"
    bool registered_ = false;
};

} // namespace iris
