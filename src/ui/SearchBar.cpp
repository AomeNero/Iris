// Iris UI —— SearchBar 实现
#include "ui/SearchBar.h"
#include "ui/Theme.h"

#include <QPainter>
#include <QFontMetrics>
#include <QRect>

namespace iris {

SearchBar::SearchBar() {
    icon_.load(":/iris.png");  // 由 resources.qrc 提供（前缀 :/）
}

void SearchBar::Paint(QPainter& p, const QRect& rect, const QString& text, const QString& preedit,
                      bool cursorVisible, bool hasFocus) {
    p.setRenderHint(QPainter::Antialiasing);

    // iris 图标尺寸（随输入框高度自适应）
    const int iconSize = rect.height() - 2 * kPadH - 8;

    // 1. 输入框：左/上/下 缩进 kPadH(20)；右侧缩进 (40+iconSize) 为 iris 让出
    //    [距框20 + iris宽 + 距窗口右20]，使 iris 落在框外右侧（灰底上）
    const QRect box = rect.adjusted(kPadH, kPadH, -(40 + iconSize), -kPadH);
    p.setPen(Qt::NoPen);
    p.setBrush(CurrentPalette().inputBg);
    p.drawRoundedRect(box, 12, 12);

    // 2. 文本（iris 已在框外，文字延伸到距 box 右边框 kPadH）
    p.setFont(font_);
    const QFontMetrics fm(font_);
    const int cursorH = box.height() - 16;
    const int textLeft  = box.left() + kPadH;
    const int textRight = box.right() - kPadH;

    const int textW    = fm.horizontalAdvance(text);
    const int preeditW = preedit.isEmpty() ? 0 : fm.horizontalAdvance(preedit);

    if (!text.isEmpty() || !preedit.isEmpty()) {
        const int maxTextW = textRight - textLeft;
        // 已提交文本（正常色，超宽右截断）
        if (!text.isEmpty()) {
            p.setPen(CurrentPalette().inputText);
            p.drawText(QRect(textLeft, box.top(), maxTextW, box.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(text, Qt::ElideRight, maxTextW));
        }
        // 预编辑文本（拼音实时预览）：下划线，紧跟已提交文本
        if (!preedit.isEmpty()) {
            QFont pf = font_;
            pf.setUnderline(true);
            p.setFont(pf);
            p.setPen(CurrentPalette().inputText);
            const int preeditX = textLeft + textW;
            p.drawText(QRect(preeditX, box.top(), qMax(0, textRight - preeditX), box.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, preedit);
            p.setFont(font_);
        }
        if (cursorVisible && hasFocus) {
            const int cursorX = qMin(textLeft + textW + preeditW + 1, textRight);
            const int cursorY = box.top() + (box.height() - cursorH) / 2;
            p.fillRect(QRect(cursorX, cursorY, kCursorW, cursorH), CurrentPalette().inputText);
        }
    } else if (cursorVisible && hasFocus) {
        const int cursorY = box.top() + (box.height() - cursorH) / 2;
        p.fillRect(QRect(textLeft, cursorY, kCursorW, cursorH), CurrentPalette().inputText);
    }

    // 3. iris.png 在输入框外侧右侧：距框右边框 20px、距窗口右边框 20px
    const QRect iconRect(box.right() + kPadH,
                         box.top() + (box.height() - iconSize) / 2,
                         iconSize, iconSize);
    if (!icon_.isNull()) {
        p.drawPixmap(iconRect, icon_);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(CurrentPalette().irisFallback);
        p.drawRoundedRect(iconRect, 12, 12);
    }
}

} // namespace iris
