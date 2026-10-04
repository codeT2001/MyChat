#ifndef TIMERBUTTON_H
#define TIMERBUTTON_H

#include <QPushButton>
#include <QTimer>
class TimerButton : public QPushButton {
    Q_OBJECT
public:
    TimerButton(QWidget *parent = nullptr);
    ~TimerButton();
    virtual void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QTimer *timer_;
    int counter_;
};

#endif // TIMERBUTTON_H
