#ifndef COUNTDOWNBUTTON_H
#define COUNTDOWNBUTTON_H

#include <QPushButton>
#include <QTimer>
// 验证码倒计时按钮：点击后进入倒计时禁用期，结束后自动恢复可点击
class CountdownButton : public QPushButton {
    Q_OBJECT
public:
    CountdownButton(QWidget *parent = nullptr);
    ~CountdownButton();
    virtual void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QTimer *timer_;
    int counter_;
};

#endif // COUNTDOWNBUTTON_H
