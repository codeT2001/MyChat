#include "badgebutton.h"
#include <QPainter>

BadgeButton::BadgeButton(QWidget *parent) : QPushButton(parent), showBadge_(false), badgeSize_(10)
{
    setCheckable(true);
    setAutoExclusive(false);
}

BadgeButton::~BadgeButton() {}

void BadgeButton::setShowBadge(bool show)
{
    if (showBadge_ != show) {
        showBadge_ = show;
        update(); // 触发重绘
    }
}

bool BadgeButton::isBadgeShown() const
{
    return showBadge_;
}

void BadgeButton::setBadgeSize(int size)
{
    if (badgeSize_ != size && size > 0) {
        badgeSize_ = size;
        update();
    }
}

void BadgeButton::paintEvent(QPaintEvent *event)
{
    // 1. 绘制按钮本体
    QPushButton::paintEvent(event);

    // 2. 如果不需要显示，直接返回
    if (!showBadge_) {
        return;
    }

    // 3. 配置画笔
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true); // 抗锯齿
    painter.setBrush(QColor(Qt::red));
    painter.setPen(Qt::NoPen);

    // 4. 计算右上角坐标
    int margin = 2;
    int x = this->width() - badgeSize_ - margin;
    int y = margin;

    // 边界检查：防止按钮太小时红点画出外面
    if (x < margin)
        x = margin;
    if (y + badgeSize_ > this->height()) {
        y = this->height() - badgeSize_ - margin;
    }

    // 5. 绘制红点
    painter.drawEllipse(x, y, badgeSize_, badgeSize_);
}
