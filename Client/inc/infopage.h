#ifndef INFOPAGE_H
#define INFOPAGE_H

#include <QWidget>
#include <memory>
#include <functional>

struct FriendInfo;
struct UserInfo;
namespace Ui {
class InfoPage;
}
class QPaintEvent;
class QEvent;
class QObject;
class QAction;
class QLineEdit;

class InfoPage : public QWidget {
    Q_OBJECT
public:
    explicit InfoPage(QWidget *parent = nullptr);
    ~InfoPage();

    // 朋友资料：资料行可直接编辑（hover 行尾显示编辑图标作为提示），底部操作可见
    void SetInfo(std::shared_ptr<FriendInfo> info);
    // 自己的资料：隐藏备注/标签编辑行与发消息/语音/视频操作
    void SetSelfInfo(std::shared_ptr<UserInfo> info);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // 底部三个操作按钮，参数为好友 uid
    void SigSendMessage(int uid);
    void SigVoiceCall(int uid);
    void SigVideoCall(int uid);

private slots:
    // 回车/失焦：若文本有变化则提交
    void CommitRemark();
    void CommitLabel();

private:
    // 公共填充：头像/昵称/备注/标签
    void FillContent(int uid,
                     const QString &name,
                     const QString &nick,
                     const QString &icon,
                     const QString &label);
    void SetActionsVisible(bool visible);
    void SetEditableRows(bool editable);
    // 给 QLineEdit 尾部挂编辑图标（TrailingPosition），仅作"可编辑"提示，默认隐藏
    void AttachEditAction(QLineEdit *edit, QAction *&action, const QString &tip);
    // 统一提交：trim 后与进入页面时的基线值比较，有变化才落库
    void CommitEdit(QLineEdit *edit,
                    QString &baseText,
                    const std::function<void(const QString &)> &apply);

    Ui::InfoPage *ui;
    int uid_ = 0;
    QAction *remarkEditAction_ = nullptr;
    QAction *labelEditAction_ = nullptr;
    QString remarkBase_; // 当前已提交的备注，用于 editingFinished 时判断是否变化
    QString labelBase_; // 当前已提交的标签，用于 editingFinished 时判断是否变化
};

#endif // INFOPAGE_H
