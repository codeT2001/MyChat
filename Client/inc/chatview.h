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
    void PrependChatItem(QWidget *item);
    void InsertChatItem(QWidget *before, QWidget *item);
    // 清空所有消息条目（保留底部 stretch）
    void Clear();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *event) override;
signals:

private slots:
    void OnVScrollBarMoved(int min, int max);

private:
    void InitStyleSheet();
    QVBoxLayout *contentLayout_;
    QWidget *contentWidget_;
    QScrollArea *scrollArea_;
    bool isAppended_;
};

#endif // CHATVIEW_H
