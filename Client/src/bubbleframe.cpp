#include "bubbleframe.h"
#include "logger.h"
#include <QPainter>
#include <QColor>
namespace {
static constexpr int TRIANGLE_WIDTH = 8;
static constexpr int TRIANGLE_OFFSET = 12;
static constexpr int TRIANGLE_SIZE = 10;
static constexpr int ROUND_RADIUS = 8;
// 与 _design_tokens 对齐：自己=品牌蓝，对方=白卡片色
static constexpr QColor SELF_BUBBLE_COLOR(59, 130, 246);    // #3b82f6
static constexpr QColor OTHER_BUBBLE_COLOR(255, 255, 255);  // #ffffff
} // namespace
BubbleFrame::BubbleFrame(bool self, QWidget *parent) : QFrame{parent}, self_{self}, margin_{3}
{
    layout_ = new QHBoxLayout();
    if (self_) {
        layout_->setContentsMargins(margin_, margin_, TRIANGLE_WIDTH + margin_, margin_);
    } else {
        layout_->setContentsMargins(TRIANGLE_WIDTH + margin_, margin_, margin_, margin_);
    }
    this->setLayout(layout_);
}

void BubbleFrame::SetMargin(int margin)
{
    margin_ = margin;
}

void BubbleFrame::SetWidget(QWidget *w)
{
    if (!w) {
        LOG_WARN() << "BubbleFrame::SetWidget: widget is null";
        return;
    }
    if (layout_->count() > 0) {
        LOG_WARN() << "BubbleFrame::SetWidget: widget already set";
        return;
    }
    layout_->addWidget(w);
}

void BubbleFrame::paintEvent(QPaintEvent *e)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(Qt::NoPen);

    if (!self_) {
        painter.setBrush(QBrush(OTHER_BUBBLE_COLOR));
        QRect rect = QRect(TRIANGLE_WIDTH, 0, this->width() - TRIANGLE_WIDTH, this->height());
        painter.drawRoundedRect(rect, ROUND_RADIUS, ROUND_RADIUS);
        // 画小三角
        QPointF points[3] = {
            QPointF(rect.x(), TRIANGLE_OFFSET),
            QPointF(rect.x(), TRIANGLE_OFFSET + TRIANGLE_WIDTH),
            QPointF(rect.x() - TRIANGLE_WIDTH, TRIANGLE_SIZE + TRIANGLE_WIDTH - TRIANGLE_WIDTH / 2),
        };
        painter.drawPolygon(points, 3);
    } else {
        painter.setBrush(QBrush(SELF_BUBBLE_COLOR));
        // 画气泡
        QRect rect = QRect(0, 0, this->width() - TRIANGLE_WIDTH, this->height());
        painter.drawRoundedRect(rect, ROUND_RADIUS, ROUND_RADIUS);
        // 画三角
        QPointF points[3] = {
            QPointF(rect.x() + rect.width(), TRIANGLE_OFFSET),
            QPointF(rect.x() + rect.width(), TRIANGLE_OFFSET + TRIANGLE_WIDTH),
            QPointF(rect.x() + rect.width() + TRIANGLE_WIDTH, TRIANGLE_SIZE + TRIANGLE_WIDTH - TRIANGLE_WIDTH / 2),
        };
        painter.drawPolygon(points, 3);
    }
    return QFrame::paintEvent(e);
}
