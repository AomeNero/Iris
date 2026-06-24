// Iris UI —— SearchWindow 实现
#include "ui/SearchWindow.h"
#include "ui/Theme.h"

#include "core/WinUtil.h"
#include "core/StringUtil.h"

#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPropertyAnimation>
#include <QInputMethodEvent>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QRect>

#include "ui/ContextMenu.h"  // 右键结果菜单

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <msctf.h>    // TSF IME 控制：GUID_COMPARTMENT_KEYBOARD_OPENCLOSE

namespace {
// 右键菜单动作 id（与 ContextMenu::addItem 的 action 对应）
enum ContextAction { ACT_OPEN = 1, ACT_REVEAL, ACT_COPY, ACT_PROPS };
}

namespace iris {

SearchWindow::SearchWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_InputMethodEnabled, true);  // 启用输入法，支持中文 IME 输入

    setMouseTracking(true);  // 悬停高亮需要无按钮鼠标移动事件

    connect(&cursorTimer_, &QTimer::timeout, this, [this]() {
        cursorVisible_ = !cursorVisible_;
        update();
    });
    cursorTimer_.setInterval(530);
    cursorTimer_.start();

    RebuildLayout();
    setFocusPolicy(Qt::StrongFocus);
}

SearchWindow::~SearchWindow() = default;

void SearchWindow::RebuildLayout() {
    const int listH = resultList_.CalculateHeight();
    setFixedSize(kWindowWidth + 2 * kShadowMargin,
                 kInputHeight + listH + 2 * kShadowMargin);
}

QRect SearchWindow::GetInputRect() const {
    return QRect(kShadowMargin, kShadowMargin, kWindowWidth, kInputHeight);
}
QRect SearchWindow::GetListRect() const {
    return QRect(kShadowMargin, kShadowMargin + kInputHeight,
                 kWindowWidth, resultList_.CalculateHeight());
}

void SearchWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 内容区（内缩 kShadowMargin，作为内容到窗口边的留白带）
    const QRect content(kShadowMargin, kShadowMargin, kWindowWidth,
                        height() - 2 * kShadowMargin);

    // 裁剪到内容圆角区，后续绘制全部落在内容区内
    QPainterPath contentPath;
    contentPath.addRoundedRect(content, kCornerRadius, kCornerRadius);
    p.setClipPath(contentPath);

    // Layer 1: 灰基底 #e9e9e9（已移除原白底配套的顶部光泽——灰底上白光泽会把
    //         背景拉向白色、偏离 #e9e9e9）
    p.fillRect(content, CurrentPalette().base);

    // Layer 3-4: 输入栏
    const QRect inputRect = GetInputRect();
    searchBar_.Paint(p, inputRect, inputText_, preeditText_, cursorVisible_, hasFocus());

    // Layer 5: 结果列表
    resultList_.Paint(p, GetListRect());

    // Layer 6: 极细边框
    p.setClipping(false);
    QPen borderPen(CurrentPalette().border);
    borderPen.setWidthF(0.5);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(content.adjusted(0, 0, -1, -1), kCornerRadius, kCornerRadius);
}

void SearchWindow::showWithFadeIn() {
    inputText_.clear();
    preeditText_.clear();
    resultList_.Clear();
    imeStateSaved_ = false;  // 每次弹出重新保存 IME 原状态
    RebuildLayout();

    // 在 setFocus 之前保存系统当前 IME 状态——setFocus 会激活 IME，
    // 激活过程可能自动打开 IME（英文→中文），污染原状态。
    QueryAndSaveImeState();

    // 居中到当前显示器
    const RECT mr = WinUtil::GetCurrentMonitorRect();
    const int x = mr.left + (mr.right - mr.left - width()) / 2;
    const int y = mr.top + (mr.bottom - mr.top - height()) / 3;  // 偏上 1/3
    move(x, y);

    setWindowOpacity(0.0);
    show();
    raise();
    activateWindow();
    setFocus();

    auto* anim = new QPropertyAnimation(this, "windowOpacity", this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // 默认英文输入：defer 到下一事件循环等 OS 完成 IME 激活后再关闭
    QTimer::singleShot(0, this, [this]() { EnsureEnglishInput(); });
}

void SearchWindow::hideWithFadeOut() {
    RestoreIme();  // 恢复弹出前的 IME 状态（若原为中文则切回中文）
    preeditText_.clear();

    auto* anim = new QPropertyAnimation(this, "windowOpacity", this);
    anim->setDuration(150);
    anim->setStartValue(windowOpacity());
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void SearchWindow::QueryAndSaveImeState() {
    if (imeStateSaved_) return;

    ITfThreadMgr* threadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr, (void**)&threadMgr);
    if (FAILED(hr) || !threadMgr) return;

    TfClientId clientId = 0;
    threadMgr->Activate(&clientId);

    ITfCompartmentMgr* compMgr = nullptr;
    hr = threadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
    if (SUCCEEDED(hr) && compMgr) {
        ITfCompartment* comp = nullptr;
        hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
        if (SUCCEEDED(hr) && comp) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(comp->GetValue(&var)) && var.vt == VT_I4) {
                imeWasOpen_ = (var.lVal != 0);
            }
            VariantClear(&var);
            comp->Release();
        }
        compMgr->Release();
    }
    threadMgr->Release();

    imeStateSaved_ = true;
}

void SearchWindow::EnsureEnglishInput() {
    // 通过 TSF 关闭 IME，使搜索框默认英文输入（用户 Shift 可切中文）
    // ImmSetConversionStatus/ImmSetOpenStatus 对 TSF IME 无效，必须走 TSF COM。
    // 状态保存由 QueryAndSaveImeState() 在 setFocus 前完成，此处仅关闭。
    ITfThreadMgr* threadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr, (void**)&threadMgr);
    if (FAILED(hr) || !threadMgr) return;

    TfClientId clientId = 0;
    threadMgr->Activate(&clientId);

    ITfCompartmentMgr* compMgr = nullptr;
    hr = threadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
    if (SUCCEEDED(hr) && compMgr) {
        ITfCompartment* comp = nullptr;
        hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
        if (SUCCEEDED(hr) && comp) {
            VARIANT var;
            VariantInit(&var);
            var.vt = VT_I4;
            var.lVal = 0;  // 关闭 IME → 英文模式
            comp->SetValue(clientId, &var);
            comp->Release();
        }
        compMgr->Release();
    }
    threadMgr->Release();
}

void SearchWindow::RestoreIme() {
    if (!imeWasOpen_) return;  // 弹出前 IME 本就关闭，无需恢复

    ITfThreadMgr* threadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr, (void**)&threadMgr);
    if (FAILED(hr) || !threadMgr) return;

    TfClientId clientId = 0;
    threadMgr->Activate(&clientId);

    ITfCompartmentMgr* compMgr = nullptr;
    hr = threadMgr->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
    if (SUCCEEDED(hr) && compMgr) {
        ITfCompartment* comp = nullptr;
        hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
        if (SUCCEEDED(hr) && comp) {
            VARIANT var;
            VariantInit(&var);
            var.vt = VT_I4;
            var.lVal = 1;  // 恢复 IME 开启（中文输入模式）
            comp->SetValue(clientId, &var);
            comp->Release();
        }
        compMgr->Release();
    }
    threadMgr->Release();

    imeWasOpen_ = false;   // 重置，避免重复恢复
    imeStateSaved_ = false; // 重置，下次弹出重新保存
}

void SearchWindow::onSearchFinished(const QVector<ResultItem>& results) {
    resultList_.SetEmptyHint(inputText_.isEmpty()
        ? QString::fromUtf8("开始输入以搜索")
        : QString::fromUtf8("无搜索结果"));
    resultList_.SetResults(results);
    RebuildLayout();
    update();
}

void SearchWindow::OpenSelected() {
    if (const ResultItem* item = resultList_.GetSelected()) {
        emit itemActivated(*item);
        hideWithFadeOut();
    }
}

void SearchWindow::OpenByVisibleNo(int visibleNo) {
    if (const ResultItem* item = resultList_.GetVisibleItem(visibleNo)) {
        emit itemActivated(*item);
        hideWithFadeOut();
    }
}

void SearchWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            hideWithFadeOut();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (event->modifiers() & Qt::ControlModifier) {
                // Ctrl+Enter：打开所在文件夹（P2 完善；此处仅隐藏）
                hideWithFadeOut();
            } else {
                OpenSelected();
            }
            return;
        case Qt::Key_Up:
            resultList_.MoveSelectionUp();
            update();
            return;
        case Qt::Key_Down:
            resultList_.MoveSelectionDown();
            update();
            return;
        case Qt::Key_Left:
        case Qt::Key_PageUp:
            // 翻上一页：仅裸 ← / PageUp（排除 Alt/Ctrl/Shift/Meta；Alt+←已让位给鼠标侧键 XButton1）
            if (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier))
                break;
            resultList_.PageUp();
            RebuildLayout();  // 末页窗口高度可能变（随实际行数缩短）
            update();
            return;
        case Qt::Key_Right:
        case Qt::Key_PageDown:
            if (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier))
                break;
            resultList_.PageDown();
            RebuildLayout();
            update();
            return;
        case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
        case Qt::Key_4: case Qt::Key_5: case Qt::Key_6:
        case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
            if (event->modifiers() & Qt::ControlModifier) {
                OpenByVisibleNo(event->key() - Qt::Key_0);  // ctrlN 提示对应 Ctrl+N
                return;
            }
            break;  // 非 Ctrl：作为数字输入字符，落到下方追加逻辑
        case Qt::Key_Backspace:
            if (!inputText_.isEmpty()) {
                inputText_.chop(1);
                emit searchRequested(inputText_);
                update();
            }
            return;
        default:
            break;
    }
    if (event->text().isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    // 追加可打印字符（仅 ASCII 控制外的可见字符）
    for (const QChar ch : event->text()) {
        if (ch.isPrint()) inputText_ += ch;
    }
    emit searchRequested(inputText_);
    update();
}

void SearchWindow::mousePressEvent(QMouseEvent* event) {
    // 鼠标侧键翻页（整个窗口任意位置响应）
    if (event->button() == Qt::BackButton) {       // XButton1 后退侧键 → 上一页
        resultList_.PageUp();
        RebuildLayout();
        update();
        return;
    }
    if (event->button() == Qt::ForwardButton) {    // XButton2 前进侧键 → 下一页
        resultList_.PageDown();
        RebuildLayout();
        update();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    const int y = event->pos().y() - kShadowMargin;
    if (y >= kInputHeight) {
        resultList_.SelectByY(y - kInputHeight);
        update();
        OpenSelected();
    }
}

void SearchWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int y = event->pos().y() - kShadowMargin;
    if (y >= kInputHeight) {
        resultList_.SelectByY(y - kInputHeight);
        OpenSelected();
    }
}

void SearchWindow::mouseMoveEvent(QMouseEvent* event) {
    const int y = event->pos().y() - kShadowMargin;
    // 悬停即选中（仅在列表区域内）；移出列表区不改变选中
    if (y >= kInputHeight) {
        if (resultList_.SetHoverByY(y - kInputHeight)) update();
    }
}

void SearchWindow::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        resultList_.ScrollBy(-steps);  // 向上滚 → 选择上移
        update();
    }
}

void SearchWindow::focusOutEvent(QFocusEvent* event) {
    // 失焦（点击外部其他窗口）淡出隐藏；但以下不隐藏：
    //  - 焦点转给弹出菜单（PopupFocusReason，如托盘右键菜单）：
    //    否则点菜单项弹对话框时本窗口早已被隐藏，对话框关闭后无法恢复焦点
    //  - 弹应用自身模态对话框期间（suppressAutoHide_ 守卫）
    if (!suppressAutoHide_
        && event->reason() != Qt::PopupFocusReason
        && !isHidden()) {
        hideWithFadeOut();
    }
    QWidget::focusOutEvent(event);
}

bool SearchWindow::nativeEvent(const QByteArray& /*type*/, void* message, long* result) {
#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        const LONG border = kResizeBorder;
        RECT wr;
        GetWindowRect(HWND(winId()), &wr);
        const LONG x = GET_X_LPARAM(msg->lParam);
        const LONG y = GET_Y_LPARAM(msg->lParam);
        if (x < wr.left + border && y < wr.top + border)  { *result = HTTOPLEFT;     return true; }
        if (x > wr.right - border && y < wr.top + border)  { *result = HTTOPRIGHT;    return true; }
        if (x < wr.left + border && y > wr.bottom - border){ *result = HTBOTTOMLEFT;  return true; }
        if (x > wr.right - border && y > wr.bottom - border){*result = HTBOTTOMRIGHT; return true; }
        if (x < wr.left + border)   { *result = HTLEFT;   return true; }
        if (x > wr.right - border)  { *result = HTRIGHT;  return true; }
        if (y < wr.top + border)    { *result = HTTOP;    return true; }
        if (y > wr.bottom - border) { *result = HTBOTTOM; return true; }
    }
    // IME 激活通知（首次启动或重获焦点时）：defer 关闭 IME 确保英文默认
    // 仅靠 showWithFadeIn 中 timer 不够——首次启动时 timer 可能在 WM_IME_SETCONTEXT 前执行
    if (msg->message == WM_IME_SETCONTEXT && msg->wParam) {
        QTimer::singleShot(0, this, [this]() { EnsureEnglishInput(); });
    }
#endif
    return false;
}

QVariant SearchWindow::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle) {
        const QFontMetrics fm(searchBar_.Font());
        const int textW = fm.horizontalAdvance(inputText_) + fm.horizontalAdvance(preeditText_);
        // 与 SearchBar::Paint 一致：box 左/上/下缩进 kPadH，右缩进 (40+iconSize) 为 iris 让位
        const int iconSize = kInputHeight - 2 * SearchBar::kPadH - 8;
        const QRect box = GetInputRect().adjusted(SearchBar::kPadH, SearchBar::kPadH,
                                                   -(40 + iconSize), -SearchBar::kPadH);
        const int cursorH = box.height() - 16;
        const int textRight = box.right() - SearchBar::kPadH;
        const int x = qMin(box.left() + SearchBar::kPadH + textW, textRight);
        const int y = box.top() + (box.height() - cursorH) / 2;
        return QRect(x, y, SearchBar::kCursorW, cursorH);
    }
    return QWidget::inputMethodQuery(query);
}

void SearchWindow::inputMethodEvent(QInputMethodEvent* e) {
    // IME 提交文本（中文输入法选字后）：追加到输入框，与 keyPressEvent 的字符追加一致
    if (!e->commitString().isEmpty()) {
        inputText_ += e->commitString();
        emit searchRequested(inputText_);
    }
    // 预编辑文本（拼音实时预览）；提交时 IME 会清空 preeditString → preeditText_ 自动清空
    preeditText_ = e->preeditString();
    e->accept();
    update();
}

void SearchWindow::contextMenuEvent(QContextMenuEvent* e) {
    const int y = e->pos().y() - kShadowMargin;
    if (y < kInputHeight) return;  // 输入框区不弹菜单
    resultList_.SelectByY(y - kInputHeight);  // 右键选中该行（资源管理器习惯）
    const ResultItem* item = resultList_.GetSelected();
    if (!item) return;

    const ResultItem itemCopy = *item;  // 值捕获，防菜单期间结果刷新悬垂
    SetSuppressAutoHide(true);  // 防弹菜单失焦触发 focusOut 自动隐藏（同"关于"对话框）

    auto* menu = new ContextMenu(this);
    menu->addItem(QString::fromUtf8("打开(O)"), QChar('O'), ACT_OPEN);
    if (itemCopy.type != ItemType::BOOKMARK) {
        menu->addItem(QString::fromUtf8("打开路径(P)"), QChar('P'), ACT_REVEAL);
        menu->addItem(QString::fromUtf8("复制路径和文件名(C)"), QChar('C'), ACT_COPY);
        menu->addItem(QString::fromUtf8("属性(R)"), QChar('R'), ACT_PROPS);
    } else {
        // BOOKMARK(URL) 无文件，无"打开路径"/"属性"
        menu->addItem(QString::fromUtf8("复制路径和文件名(C)"), QChar('C'), ACT_COPY);
    }

    connect(menu, &ContextMenu::triggered, this, [this, itemCopy](int action) {
        switch (action) {
            case ACT_OPEN:
                OpenSelected();  // emit itemActivated + hideWithFadeOut
                break;
            case ACT_REVEAL:
                WinUtil::RevealInExplorer(itemCopy.path);
                hideWithFadeOut();
                break;
            case ACT_COPY:
                QApplication::clipboard()->setText(QString::fromStdWString(itemCopy.path));
                break;  // 复制后保留窗口（用户可能继续操作）
            case ACT_PROPS:
                WinUtil::ShowProperties(itemCopy.path);
                break;  // 属性对话框显示，保留窗口
        }
    });
    connect(menu, &ContextMenu::closed, this, [this]() { SetSuppressAutoHide(false); });

    menu->popup(e->globalPos());
}

} // namespace iris
