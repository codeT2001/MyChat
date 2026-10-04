#include "textbubble.h"
#include <QTextEdit>
#include <QEvent>
#include <QTextLayout>
#include <QTextBlock>

TextBubble::TextBubble(const QString &text, bool self, QWidget *parent) : BubbleFrame{self, parent}
{
    textEdit_ = new QTextEdit();
    textEdit_->setReadOnly(true);
    textEdit_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textEdit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textEdit_->installEventFilter(this);

    QFont font("Microsoft YaHei");
    font.setPointSize(12);
    textEdit_->setFont(font);
    SetPlainText(text);
    SetWidget(textEdit_);
    InitStyleSheet();
}

bool TextBubble::eventFilter(QObject *o, QEvent *e)
{
    if (textEdit_ == o && e->type() == QEvent::Paint) {
        AdjustTextHeight(); // PaintEvent中设置
    }
    return BubbleFrame::eventFilter(o, e);
}

void TextBubble::AdjustTextHeight()
{
    qreal docMargin = textEdit_->document()->documentMargin(); // 字体到边框的距离默认为4
    QTextDocument *doc = textEdit_->document();
    qreal height = 0;
    // 把每一段的高度相加=文本高
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next()) {
        QTextLayout *textLayout = it.layout();
        QRectF rect = textLayout->boundingRect(); // 这段的rect
        height += rect.height();
    }
    int margin = this->layout()->contentsMargins().top();
    // 设置这个气泡需要的高度 文本高+文本边距+TextEdit边框到气泡边框的距离
    setFixedHeight(height + docMargin * 2 + margin * 2);
}

void TextBubble::SetPlainText(const QString &text)
{
    textEdit_->setPlainText(text);
    // m_pTextEdit->setHtml(text);
    // 找到段落中最大宽度
    qreal doc_margin = textEdit_->document()->documentMargin();
    int marginLeft = this->layout()->contentsMargins().left();
    int marginRight = this->layout()->contentsMargins().right();
    QFontMetricsF fm(textEdit_->font());
    QTextDocument *doc = textEdit_->document();
    int maxWidth = 0;
    // 遍历每一段找到 最宽的那一段
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next()) // 字体总长
    {
        int txtWidth = int(fm.horizontalAdvance(it.text()));
        maxWidth = qMax(maxWidth, txtWidth); // 找到最长的那段
    }
    // 设置这个气泡的最大宽度 只需要设置一次
    setMaximumWidth(maxWidth + doc_margin * 2 + (marginLeft + marginRight)); // 设置最大宽度
}

void TextBubble::InitStyleSheet()
{
    textEdit_->setStyleSheet(R"(
        QTextEdit {
            background: transparent;
            border: none;
            padding: 0px;
        }
    )");
}
