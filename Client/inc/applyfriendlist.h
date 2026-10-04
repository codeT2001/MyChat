#ifndef APPLYFRIENDLIST_H
#define APPLYFRIENDLIST_H

#include <QListWidget>
class QEvent;
class ApplyFriendList : public QListWidget {
    Q_OBJECT
public:
    explicit ApplyFriendList(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
signals:
    void SigShowSearch(bool);
};

#endif // APPLYFRIENDLIST_H
