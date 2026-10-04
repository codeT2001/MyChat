#ifndef APPLYFRIENDPAGE_H
#define APPLYFRIENDPAGE_H

#include <QWidget>

namespace Ui {
class ApplyFriendPage;
}
struct AddFriendApply;
struct ApplyInfo;
struct FriendInfo;
class ApplyFriendItem;
class QPaintEvent;
class ApplyFriendPage : public QWidget {
    Q_OBJECT

public:
    explicit ApplyFriendPage(QWidget *parent = nullptr);
    ~ApplyFriendPage();
    void AddNewApply(std::shared_ptr<AddFriendApply> apply);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void LoadApplyList();
    Ui::ApplyFriendPage *ui;
    std::unordered_map<int, ApplyFriendItem *> unauthItems_;
public slots:
    void SlotFriendAuth(std::shared_ptr<FriendInfo> info);
    void SlotAuthFriend(std::shared_ptr<ApplyInfo> info);
    void SlotRejectFriend(std::shared_ptr<ApplyInfo> info);
signals:
    void SigShowSearch(bool);
};

#endif // APPLYFRIENDPAGE_H
