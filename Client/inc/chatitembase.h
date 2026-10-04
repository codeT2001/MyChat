#ifndef CHATITEMBASE_H
#define CHATITEMBASE_H

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
class ChatItemBase : public QWidget {
    Q_OBJECT
public:
    explicit ChatItemBase(bool self = true, QWidget *parent = nullptr);
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

#endif // CHATITEMBASE_H
