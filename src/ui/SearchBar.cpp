// Iris UI —— SearchBar 实现
#include "ui/SearchBar.h"

#include <QPainter>
#include <QFontMetrics>
#include <QRect>

namespace iris {

SearchBar::SearchBar() {
    icon_.load(":/iris.png");  // 由 resources.qrc 提供（前缀 :/）
}

void SearchBar::Paint(QPainter& p, const QRect& rect, const QString& text,
                      bool cursorVisible, bool hasFocus) {
    p.setRenderHint(QPainter::Antialiasing);

    // iris 图标尺寸（随输入框高度自适应）
    const int iconSize = rect.height() - 2 * kPadH - 8;

    // 1. 输入框：左/上/下 缩进 kPadH(20)；右侧缩进 (40+iconSize) 为 iris 让出
    //    [距框20 + iris宽 + 距窗口右20]，使 iris 落在框外右侧（灰底上）
    const QRect box = rect.adjusted(kPadH, kPadH, -(40 + iconSize), -kPadH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d2d2d2"));
    p.drawRoundedRect(box, 12, 12);

    // 2. 文本（iris 已在框外，文字延伸到距 box 右边框 kPadH）
    p.setFont(font_);
    const QFontMetrics fm(font_);
    const int cursorH = box.height() - 16;
    const int textLeft  = box.left() + kPadH;
    const int textRight = box.right() - kPadH;

    if (!text.isEmpty()) {
        p.setPen(Qt::black);
        const int maxTextW = textRight - textLeft;
        p.drawText(QRect(textLeft, box.top(), maxTextW, box.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(text, Qt::ElideRight, maxTextW));
        if (cursorVisible && hasFocus) {
            const int textW = fm.horizontalAdvance(text);
            const int cursorX = qMin(textLeft + textW + 1, textRight);
            const int cursorY = box.top() + (box.height() - cursorH) / 2;
            p.fillRect(QRect(cursorX, cursorY, kCursorW, cursorH), Qt::black);
        }
    } else if (cursorVisible && hasFocus) {
        const int cursorY = box.top() + (box.height() - cursorH) / 2;
        p.fillRect(QRect(textLeft, cursorY, kCursorW, cursorH), Qt::black);
    }

    // 3. iris.png 在输入框外侧右侧：距框右边框 20px、距窗口右边框 20px
    const QRect iconRect(box.right() + kPadH,
                         box.top() + (box.height() - iconSize) / 2,
                         iconSize, iconSize);
    if (!icon_.isNull()) {
        p.drawPixmap(iconRect, icon_);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(180, 180, 180));
        p.drawRoundedRect(iconRect, 12, 12);
    }
}

} // namespace iris
