#ifndef STATEFULCLICKLABEL_H
#define STATEFULCLICKLABEL_H

#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
enum class ClickLabelState { NORMAL = 0, SELECTED = 1 };
// 可点击且带 正常/悬停/按下/选中 多状态样式的 QLabel
class StatefulClickLabel : public QLabel {
    Q_OBJECT

public:
    explicit StatefulClickLabel(QWidget *parent = nullptr);
    explicit StatefulClickLabel(const QString &text, QWidget *parent = nullptr);

    // 配置各交互状态下通过动态属性 "state" 切换的 QSS 属性值
    void SetStateStyles(const QString &normal = "",
                        const QString &hover = "",
                        const QString &press = "",
                        const QString &select = "",
                        const QString &selectHover = "",
                        const QString &selectedPress = "");

    ClickLabelState GetCurState() const
    {
        return curState_;
    }

protected:
    virtual void enterEvent(QEnterEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void mouseReleaseEvent(QMouseEvent *event) override;

Q_SIGNALS:
    // 点击信号
    void clicked(void);

private:
    QString normal_;
    QString normalHover_;
    QString normalPress_;

    QString selected_;
    QString selectedHover_;
    QString selectedPress_;

    ClickLabelState curState_;
};

#endif // STATEFULCLICKLABEL_H
