// Iris UI —— 右键上下文菜单（QPainter 自绘，Alfred 风格）
// 用于搜索结果项右键：打开/打开路径/复制/属性。Qt::Popup 自动处理"点外部关闭"。
#pragma once

#include <QChar>
#include <QString>
#include <QVector>
#include <QWidget>

namespace iris {

class ContextMenu : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString label;     // 显示文本（如 "打开(O)"）
        QChar   mnemonic;  // 助记符字母（'O'），用于键盘匹配；QChar() 表示无
        int     action;    // 调用方定义的动作 id
    };

    explicit ContextMenu(QWidget* parent = nullptr);

    void addItem(const QString& label, QChar mnemonic, int action);
    void popup(const QPoint& globalPos);  // 计算尺寸并在全局位置显示 + 抓焦点

signals:
    void triggered(int action);  // 用户选中某项（鼠标点击/Enter/助记符）
    void closed();               // 菜单关闭（点外部/Esc/选择后 hide）

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    int  rowAt(int y) const;     // y 对应的项索引，-1 表示空白
    void triggerCurrent();       // 执行 selectedIndex_ 对应动作并关闭

    QVector<Item> items_;
    int selectedIndex_ = -1;

    static constexpr int kPadV   = 6;    // 上下内边距
    static constexpr int kPadH   = 22;   // 左右内边距
    static constexpr int kRowH   = 38;   // 行高
    static constexpr int kRadius = 12;   // 圆角
};

} // namespace iris
