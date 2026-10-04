#ifndef CHATVIEW_H
#define CHATVIEW_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPaintEvent>
class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    void AppendChatItem(QWidget *item);
    // 清空所有消息条目（保留底部 stretch）
    void Clear();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    // 消息范围变化（新消息追加）时自动滚动到底部
    void OnScrollRangeChanged(int min, int max);

private:
    QVBoxLayout *contentLayout_;
    QWidget *contentWidget_;
    QScrollArea *scrollArea_;
    bool autoScrollToBottom_;
};

#endif // CHATVIEW_H
