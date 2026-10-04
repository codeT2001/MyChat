#include "friendlabel.h"
#include "ui_friendlabel.h"
#include "utils.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QStyle>
FriendLabel::FriendLabel(QWidget *parent)
    : QFrame(parent), ui(new Ui::FriendLabel), width_(0), height_(0), selected_(false)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/friendlabel.qss");
    setCursor(Qt::PointingHandCursor);
    UpdateStateStyle();
}

FriendLabel::~FriendLabel()
{
    delete ui;
}

void FriendLabel::SetText(const QString &text)
{
    text_ = text;
    ui->tipLb->setText(text);

    // adjustSize 在全局 QSS 字体生效前测量会偏小，导致文字被裁剪；
    // 先 ensurePolished 让 QSS 字体落地，再用字体度量计算尺寸
    ui->tipLb->ensurePolished();
    const QFontMetrics fm = ui->tipLb->fontMetrics();
    width_ = fm.horizontalAdvance(text) + 20; // 左右留白 + 边框富余
    height_ = fm.height() + 10;               // 上下留白 + 富余
    this->setFixedWidth(width_);
    this->setFixedHeight(height_);
}

int32_t FriendLabel::Width() const
{
    return width_;
}

int32_t FriendLabel::Height() const
{
    return height_;
}

const QString &FriendLabel::Text() const
{
    return text_;
}

void FriendLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        SetSelected(!selected_);
        emit SigToggle(text_, selected_ ? ClickLabelState::SELECTED : ClickLabelState::NORMAL);
    }
    QFrame::mousePressEvent(event);
}

void FriendLabel::SetSelected(bool selected)
{
    selected_ = selected;
    UpdateStateStyle();
}

void FriendLabel::UpdateStateStyle()
{
    // 动态属性 + 重刷样式表，切换 normal/selected 两套外观
    setProperty("state", selected_ ? "selected" : "normal");
    style()->unpolish(this);
    style()->polish(this);
}
