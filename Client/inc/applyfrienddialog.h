#ifndef APPLYFRIENDDIALOG_H
#define APPLYFRIENDDIALOG_H

#include <QWidget>
#include <unordered_set>
#include "constants.h"
#include "friendlabel.h"

namespace Ui {
class ApplyFriendDialog;
}
struct SearchInfo;
struct ApplyInfo;
class ApplyFriendDialog : public QWidget {
    Q_OBJECT

public:
    explicit ApplyFriendDialog(bool apply = true, QWidget *parent = nullptr);
    ~ApplyFriendDialog();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
    void SetApplyInfo(std::shared_ptr<ApplyInfo> info);
    void SendFriendRequest(RequestId reqId, int fromUid, int toUid);

private:
    Ui::ApplyFriendDialog *ui;
    void InitTipLbs();
    FriendLabel *CreateTipLabel(const QString &text);
    void AddLabel(const QString &name);
    void RelayoutTipLbs();
    std::vector<FriendLabel *> tipLbs_;
    std::vector<QString> tipDatas_;
    // 已选中的标签文本记录；取消选中即移除
    std::unordered_set<QString> selectedTipLbs_;
    std::shared_ptr<SearchInfo> searchInfo_;
    std::shared_ptr<ApplyInfo> applyInfo_;
    bool isAuthMode_ = false; // true=认证好友（同意/拒绝），false=发起加好友
signals:
    void sigRejectFriend(std::shared_ptr<ApplyInfo> info);
public slots:
    // 输入框按回车，将文本作为新标签加入列表（默认选中）
    void SlotLabelEnter();
    // 标签点击切换状态：选中则记录，取消选中则删除记录
    void SlotChangeFriendLabelByTip(const QString &name, ClickLabelState state);
    // 处理确认回调
    void SlotApplySure();
    void SlotAuthSure();
    // 处理取消回调
    void SlotCancel();
};

#endif // APPLYFRIENDDIALOG_H
