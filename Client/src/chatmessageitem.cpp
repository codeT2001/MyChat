#include "chatmessageitem.h"
#include "logger.h"
#include <QSpacerItem>
ChatMessageItem::ChatMessageItem(bool self, QWidget *parent) : QWidget{parent}, self_(self)
{
    nameLabel_ = new QLabel();
    nameLabel_->setObjectName("chatItemName");
    QFont font("Microsoft YaHei");
    font.setPointSize(9);
    nameLabel_->setFont(font);
    nameLabel_->setFixedHeight(20);

    iconLabel_ = new QLabel();
    iconLabel_->setScaledContents(true);
    iconLabel_->setFixedSize(42, 42);

    bubble_ = new QWidget();
    layout_ = new QGridLayout();
    layout_->setVerticalSpacing(3);
    layout_->setHorizontalSpacing(3);
    layout_->setContentsMargins(3, 3, 3, 3);
    QSpacerItem *space = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    if (self) {
        nameLabel_->setContentsMargins(0, 0, 8, 0);
        nameLabel_->setAlignment(Qt::AlignRight);
        layout_->addWidget(nameLabel_, 0, 1, 1, 1);
        layout_->addWidget(iconLabel_, 0, 2, 2, 1, Qt::AlignTop);
        layout_->addItem(space, 1, 0, 1, 1);
        layout_->addWidget(bubble_, 1, 1, 1, 1);
        layout_->setColumnStretch(0, 2);
        layout_->setColumnStretch(1, 3);
    } else {
        nameLabel_->setContentsMargins(8, 0, 0, 0);
        nameLabel_->setAlignment(Qt::AlignLeft);
        layout_->addWidget(iconLabel_, 0, 0, 2, 1, Qt::AlignTop);
        layout_->addWidget(nameLabel_, 0, 1, 1, 1);
        layout_->addWidget(bubble_, 1, 1, 1, 1);
        layout_->addItem(space, 2, 2, 1, 1);
        layout_->setColumnStretch(1, 3);
        layout_->setColumnStretch(2, 2);
    }
    this->setLayout(layout_);
}

bool ChatMessageItem::IsSelf() const
{
    return self_;
}

void ChatMessageItem::SetUserName(const QString &name)
{
    nameLabel_->setText(name);
}

void ChatMessageItem::SetUserIcon(const QPixmap &icon)
{
    iconLabel_->setPixmap(icon);
}

void ChatMessageItem::SetWidget(QWidget *w)
{
    if (!w) {
        LOG_WARN() << "ChatMessageItem::SetWidget: widget is null";
        return;
    }
    if (bubble_) {
        layout_->replaceWidget(bubble_, w);
        bubble_->deleteLater();
    } else {
        layout_->addWidget(w);
    }
    bubble_ = w;
}
