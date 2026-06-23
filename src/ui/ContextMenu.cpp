// Iris UI —— ContextMenu 实现（自绘 Alfred 风格右键菜单）
#include "ui/ContextMenu.h"

#include "ui/Theme.h"  // CurrentPalette

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QHideEvent>

#include <algorithm>

namespace iris {

ContextMenu::ContextMenu(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);  // 悬停高亮
    setFocusPolicy(Qt::StrongFocus);
    QFont f("Microsoft YaHei", 12);
    setFont(f);
}

void ContextMenu::addItem(const QString& label, QChar mnemonic, int action) {
    items_.append({label, mnemonic, action});
}

void ContextMenu::popup(const QPoint& globalPos) {
    // 按最长项算宽，按项数算高
    const QFontMetrics fm(font());
    int maxTextW = 0;
    for (const auto& it : items_)
        maxTextW = std::max(maxTextW, fm.horizontalAdvance(it.label));
    setFixedSize(maxTextW + 2 * kPadH, static_cast<int>(items_.size()) * kRowH + 2 * kPadV);
    selectedIndex_ = items_.isEmpty() ? -1 : 0;
    move(globalPos);
    show();
    setFocus();
}

int ContextMenu::rowAt(int y) const {
    if (items_.isEmpty()) return -1;
    const int i = (y - kPadV) / kRowH;
    return (i >= 0 && i < items_.size()) ? i : -1;
}

void ContextMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 圆角路径：背景与边框共用同一路径，半像素居中使抗锯齿锐利；圆角外保持透明（无白色尖角）
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kRadius, kRadius);

    // 背景：fillPath 只填圆角内，圆角外不绘制（透明）
    p.fillPath(path, CurrentPalette().inputBg);

    // 选中行 + 文本裁剪到圆角内，避免溢出圆角
    p.setClipPath(path);
    const QRect bounds = rect();
    if (selectedIndex_ >= 0 && selectedIndex_ < items_.size()) {
        const QRect selRect(0, kPadV + selectedIndex_ * kRowH, bounds.width(), kRowH);
        p.fillRect(selRect, CurrentPalette().base);  // 选中（灰，区别白底）
    }
    p.setPen(CurrentPalette().inputText);
    const QFontMetrics fm(font());
    for (int i = 0; i < items_.size(); ++i) {
        const QRect textRect(kPadH, kPadV + i * kRowH, bounds.width() - 2 * kPadH, kRowH);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, items_[i].label);
    }

    // 边框：同 path，与背景圆角完全对齐（杜绝尖角错位）
    p.setClipping(false);
    QPen pen(CurrentPalette().border);
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ContextMenu::mouseMoveEvent(QMouseEvent* e) {
    const int i = rowAt(e->pos().y());
    if (i >= 0 && i != selectedIndex_) { selectedIndex_ = i; update(); }
}

void ContextMenu::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { close(); return; }  // 右键外部 → Qt::Popup 已处理，这里兜底
    const int i = rowAt(e->pos().y());
    if (i >= 0) { selectedIndex_ = i; triggerCurrent(); }
    else close();
}

void ContextMenu::keyPressEvent(QKeyEvent* e) {
    if (items_.isEmpty()) { close(); return; }
    switch (e->key()) {
        case Qt::Key_Up:
            if (selectedIndex_ > 0) --selectedIndex_;
            update();
            return;
        case Qt::Key_Down:
            if (selectedIndex_ < static_cast<int>(items_.size()) - 1) ++selectedIndex_;
            update();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            triggerCurrent();
            return;
        case Qt::Key_Escape:
            close();
            return;
    }
    // 助记符匹配（按 O/P/C/R 直接触发对应项）
    if (!e->text().isEmpty()) {
        const QChar c = e->text().at(0).toUpper();
        for (int i = 0; i < items_.size(); ++i) {
            if (!items_[i].mnemonic.isNull() && items_[i].mnemonic.toUpper() == c) {
                selectedIndex_ = i;
                triggerCurrent();
                return;
            }
        }
    }
}

void ContextMenu::hideEvent(QHideEvent*) {
    emit closed();
    deleteLater();  // 一次性菜单，关闭即销毁
}

void ContextMenu::triggerCurrent() {
    if (selectedIndex_ >= 0 && selectedIndex_ < items_.size())
        emit triggered(items_[selectedIndex_].action);
    close();
}

} // namespace iris
