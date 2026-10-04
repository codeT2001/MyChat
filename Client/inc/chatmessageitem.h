#ifndef CHATMESSAGEITEM_H
#define CHATMESSAGEITEM_H

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
class ChatMessageItem : public QWidget {
    Q_OBJECT
public:
    explicit ChatMessageItem(bool self = true, QWidget *parent = nullptr);
    bool IsSelf() const;
    void SetUserName(const QString &name);
    void SetUserIcon(const QPixmap &icon);
    void SetWidget(QWidget *w);
signals:

private:
    bool self_;
    QLabel *nameLabel_;
    QLabel *iconLabel_;
    QWidget *bubble_;
    QGridLayout *layout_;
};

#endif // CHATMESSAGEITEM_H
