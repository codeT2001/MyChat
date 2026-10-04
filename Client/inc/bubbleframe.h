#ifndef BUBBLEFRAME_H
#define BUBBLEFRAME_H

#include <QFrame>
#include <QHBoxLayout>
#include <QPaintEvent>

class BubbleFrame : public QFrame {
    Q_OBJECT
public:
    explicit BubbleFrame(bool self = true, QWidget *parent = nullptr);
    virtual ~BubbleFrame() = default;

    void SetMargin(int margin);
    void SetWidget(QWidget *w);

protected:
    void paintEvent(QPaintEvent *e);

private:
    QHBoxLayout *layout_;
    bool self_;
    int margin_;
};

#endif // BUBBLEFRAME_H
