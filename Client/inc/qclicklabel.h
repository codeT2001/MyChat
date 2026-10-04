#ifndef QCLICKLABEL_H
#define QCLICKLABEL_H

#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
enum class ClickLabelState { NORMAL = 0, SELECTED = 1 };
class QClickLabel : public QLabel {
    Q_OBJECT

public:
    explicit QClickLabel(QWidget *parent = nullptr);
    explicit QClickLabel(const QString &text, QWidget *parent = nullptr);

    void SetState(const QString &normal = "",
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

#endif // QCLICKLABEL_H
