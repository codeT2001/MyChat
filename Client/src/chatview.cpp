#include "chatview.h"
#include <QHBoxLayout>
#include <QScrollBar>
#include <QStyleOption>
#include <QPainter>
#include <QTimer>
ChatView::ChatView(QWidget *parent) : QWidget{parent}, isAppended_{false}
{
    QVBoxLayout *mainLayout = new QVBoxLayout();
    this->setLayout(mainLayout);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    scrollArea_ = new QScrollArea();
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setObjectName("chatArea");
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 3. 创建内容容器 (Viewport Content)
    contentWidget_ = new QWidget();
    contentWidget_->setObjectName("chatBg");

    // 内容布局：垂直布局
    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(5); // 消息之间的间距
    contentLayout_->addStretch(1);

    scrollArea_->setWidget(contentWidget_);
    mainLayout->addWidget(scrollArea_);

    QScrollBar *scrollBar = scrollArea_->verticalScrollBar();
    connect(scrollBar, &QScrollBar::rangeChanged, this, &ChatView::OnVScrollBarMoved);
    scrollArea_->installEventFilter(this);
    InitStyleSheet();
}

void ChatView::AppendChatItem(QWidget *item)
{
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(scrollArea_->widget()->layout());
    layout->insertWidget(layout->count() - 1, item);
    isAppended_ = true;
}

void ChatView::PrependChatItem(QWidget *item) {}

void ChatView::InsertChatItem(QWidget *before, QWidget *item) {}

void ChatView::Clear()
{
    if (!contentLayout_) {
        return;
    }
    // 布局末尾保留一个 stretch，其余全部移除
    while (contentLayout_->count() > 1) {
        QLayoutItem *item = contentLayout_->takeAt(0);
        if (!item) {
            continue;
        }
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

bool ChatView::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Enter && o == scrollArea_) {
        scrollArea_->verticalScrollBar()->setHidden(scrollArea_->verticalScrollBar()->maximum() == 0);
    } else if (e->type() == QEvent::Leave && o == scrollArea_) {
        scrollArea_->verticalScrollBar()->setHidden(true);
    }
    return QWidget::eventFilter(o, e);
}

void ChatView::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatView::OnVScrollBarMoved(int min, int max)
{
    if (isAppended_) // 添加item可能调用多次
    {
        QScrollBar *pVScrollBar = scrollArea_->verticalScrollBar();
        pVScrollBar->setSliderPosition(pVScrollBar->maximum());
        // 500毫秒内可能调用多次
        QTimer::singleShot(500, this, [this]() { isAppended_ = false; });
    }
}

void ChatView::InitStyleSheet() {}
