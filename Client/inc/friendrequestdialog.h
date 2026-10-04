#ifndef FRIENDREQUESTDIALOG_H
#define FRIENDREQUESTDIALOG_H

#include <QWidget>
#include <unordered_set>
#include "constants.h"
#include "taglabel.h"

namespace Ui {
class FriendRequestDialog;
}
struct SearchInfo;
struct ApplyInfo;
class FriendRequestDialog : public QWidget {
    Q_OBJECT

public:
    // applyMode=true 发起加好友申请；false 为好友认证（同意/拒绝）模式
    explicit FriendRequestDialog(bool applyMode = true, QWidget *parent = nullptr);
    ~FriendRequestDialog();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
    void SetApplyInfo(std::shared_ptr<ApplyInfo> info);
    void SendFriendRequest(RequestId reqId, int fromUid, int toUid);

private:
    Ui::FriendRequestDialog *ui;
    void InitPresetTags();
    TagLabel *CreateTagLabel(const QString &text);
    void AddTag(const QString &name);
    void RelayoutTags();
    std::vector<TagLabel *> tagLabels_;
    std::vector<QString> presetTags_;
    // 已选中的标签文本记录；取消选中即移除
    std::unordered_set<QString> selectedTags_;
    std::shared_ptr<SearchInfo> searchInfo_;
    std::shared_ptr<ApplyInfo> applyInfo_;
    bool isAuthMode_ = false; // true=认证好友（同意/拒绝），false=发起加好友
signals:
    void SigFriendRejected(std::shared_ptr<ApplyInfo> info);
public slots:
    // 输入框按回车，将文本作为新标签加入列表（默认选中）
    void OnTagEditReturnPressed();
    // 标签点击切换状态：选中则记录，取消选中则删除记录
    void OnTagToggled(const QString &name, ClickLabelState state);
    // 处理确认回调
    void OnApplySure();
    void OnAcceptSure();
    // 处理取消回调
    void OnCancel();
};

#endif // FRIENDREQUESTDIALOG_H
