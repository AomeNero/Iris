// Iris UI —— 全局热键管理实现
#include "ui/HotkeyManager.h"

#include <QApplication>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace iris {

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    qApp->installNativeEventFilter(this);  // 始终过滤；仅匹配 kHotkeyId 才触发
}

HotkeyManager::~HotkeyManager() { Unregister(); }

bool HotkeyManager::Register(unsigned modifiers, unsigned vkCode) {
    Unregister();
    if (!::RegisterHotKey(nullptr, kHotkeyId, modifiers, vkCode)) {
        const DWORD err = GetLastError();
        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            emit hotkeyConflict(QString::fromUtf8("快捷键已被其他程序占用"));
        }
        registered_ = false;
        return false;
    }
    registered_ = true;
    return true;
}

void HotkeyManager::Unregister() {
    if (registered_) {
        ::UnregisterHotKey(nullptr, kHotkeyId);
        registered_ = false;
    }
}

bool HotkeyManager::nativeEventFilter(const QByteArray& /*eventType*/, void* message, long* /*result*/) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == static_cast<UINT_PTR>(kHotkeyId)) {
        emit hotkeyPressed();
        return true;
    }
    return false;
}

} // namespace iris
