#include "countdownbutton.h"
#include "logger.h"
#include <QDebug>
#include <QMouseEvent>

namespace {
const int32_t TIMES = 60;
const int32_t ONE_SECOND = 1000;
} // namespace
CountdownButton::CountdownButton(QWidget *parent) : QPushButton(parent), counter_(TIMES)
{
    timer_ = new QTimer(this);

    connect(timer_, &QTimer::timeout, this, [this]() {
        counter_--;
        if (counter_ <= 0) {
            timer_->stop();
            counter_ = TIMES;
            this->setText(tr("获取"));
            this->setEnabled(true);
            return;
        }
        this->setText(QString::number(counter_).append("s"));
    });
}

CountdownButton::~CountdownButton()
{
    timer_->stop();
}

void CountdownButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {

        this->setEnabled(false);
        this->setText(QString::number(counter_).append("s"));
        timer_->start(ONE_SECOND);
        emit clicked();
    }
    QPushButton::mouseReleaseEvent(e);
}
