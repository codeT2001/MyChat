#ifndef TEXTBUBBLE_H
#define TEXTBUBBLE_H

#include "bubbleframe.h"
#include <QTextEdit>

class TextBubble : public BubbleFrame {
    Q_OBJECT
public:
    explicit TextBubble(const QString &text, bool self = true, QWidget *parent = nullptr);
    virtual ~TextBubble() = default;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void AdjustTextHeight();
    void SetPlainText(const QString &text);
    void InitStyleSheet();

    static constexpr int MAX_TEXT_WIDTH = 500; // 限制最大宽度

    QTextEdit *textEdit_;
    bool adjusting_ = false; // 防止重复调整高度
};

#endif // TEXTBUBBLE_H
