// Iris UI —— 搜索输入栏（QPainter 自绘辅助类，非 QWidget）
// 设计依据: doc/detailed-design.md §8.4, doc/UI_demo/QPainter_Iris_Visuals.md §3
#pragma once

#include <QString>
#include <QFont>
#include <QPixmap>

class QPainter;
class QRect;

namespace iris {

class SearchBar {
public:
    SearchBar();

    // 由 SearchWindow::paintEvent 调用
    void Paint(QPainter& p, const QRect& rect, const QString& text,
               bool cursorVisible, bool hasFocus);

    const QFont& Font() const { return font_; }

    static constexpr int kHeight    = 140;  // 输入栏区域高度
    static constexpr int kPadH      = 20;
    static constexpr int kCursorW   = 2;
    static constexpr int kCursorH   = 85;   // 规格指定的高光标
    static constexpr int kIconSize  = 84;   // iris.png（规格 110，钳制到 100px 栏内 84）

private:
    QFont   font_{"Microsoft YaHei", 26};
    QPixmap icon_;  // 右侧 iris.png（无效时回退圆角方块）
};

} // namespace iris
