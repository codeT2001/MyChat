#include "picturebubble.h"
#include <QLabel>
namespace {
static constexpr uint32_t PIC_MAX_WIDTH = 160;
static constexpr uint32_t PIC_MAX_HEIGHT = 90;
} // namespace
PictureBubble::PictureBubble(const QPixmap &picture, bool self, QWidget *parent) : BubbleFrame(self, parent)
{
    QLabel *label = new QLabel();
    label->setScaledContents(true);
    QPixmap pix = picture.scaled(QSize(PIC_MAX_WIDTH, PIC_MAX_HEIGHT), Qt::KeepAspectRatio);
    label->setPixmap(pix);
    this->SetWidget(label);

    int leftMargin = this->layout()->contentsMargins().left();
    int rightMargin = this->layout()->contentsMargins().right();
    int margin = this->layout()->contentsMargins().bottom();
    setFixedSize(pix.width() + leftMargin + rightMargin, pix.height() + margin * 2);
}
