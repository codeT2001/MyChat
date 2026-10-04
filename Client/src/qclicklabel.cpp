#include "qclicklabel.h"
#include "utils.h"

QClickLabel::QClickLabel(QWidget *parent) : QLabel(parent), curState_(ClickLabelState::NORMAL)
{
    setMouseTracking(true);
}

QClickLabel::QClickLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent), curState_(ClickLabelState::NORMAL)
{
    setMouseTracking(true);
}

void QClickLabel::enterEvent(QEnterEvent *event)
{
    if (curState_ == ClickLabelState::NORMAL) {
        if (!normalHover_.isEmpty()) {
            setProperty("state", normalHover_);
            Utils::RefreshWidgetStyle(this);
        }
    } else {
        if (!selectedHover_.isEmpty()) {
            setProperty("state", selectedHover_);
            Utils::RefreshWidgetStyle(this);
        }
    }
    QLabel::enterEvent(event);
}

void QClickLabel::leaveEvent(QEvent *event)
{
    if (curState_ == ClickLabelState::NORMAL) {
        if (!normal_.isEmpty()) {
            setProperty("state", normal_);
            Utils::RefreshWidgetStyle(this);
        }
    } else {
        if (!selected_.isEmpty()) {
            setProperty("state", selected_);
            Utils::RefreshWidgetStyle(this);
        }
    }
    QLabel::leaveEvent(event);
}

void QClickLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (curState_ == ClickLabelState::NORMAL) {
            curState_ = ClickLabelState::SELECTED;
            if (!selectedHover_.isEmpty()) {
                setProperty("state", selectedHover_);
                Utils::RefreshWidgetStyle(this);
            }
        } else {
            curState_ = ClickLabelState::NORMAL;
            if (!normalHover_.isEmpty()) {
                setProperty("state", normalHover_);
                Utils::RefreshWidgetStyle(this);
            }
        }
    }
    QLabel::mousePressEvent(event);
}

void QClickLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QLabel::mouseReleaseEvent(event);
}

void QClickLabel::SetState(const QString &normal,
                           const QString &hover,
                           const QString &press,
                           const QString &select,
                           const QString &selectHover,
                           const QString &selectedPress)
{
    normal_ = normal;
    normalHover_ = hover;
    normalPress_ = press;

    selected_ = select;
    selectedHover_ = selectHover;
    selectedPress_ = selectedPress;

    setProperty("state", normal);
    Utils::RefreshWidgetStyle(this);
}
