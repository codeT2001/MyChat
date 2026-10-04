#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <QListWidget>
struct SearchInfo;
class FindSuccessDialog;
class LoadingDialog;
class SearchList : public QListWidget {
    Q_OBJECT
public:
    explicit SearchList(QWidget *parent = nullptr);
    void CloseFindSuccessDialog();
    void SetSearchEdit(QWidget *w);

signals:
    // 搜索命中已是好友的用户，跳转到与该好友的聊天页
    void SigJumpToChat(int uid);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
private slots:
    void SlotItemClicked(QListWidgetItem *item);
    void SlotUserSearch(std::shared_ptr<SearchInfo> info);

private:
    void WaitPending(bool pending = true);
    void AddTipItem();
    bool searchPending_;
    QWidget *searchEdit_;
    LoadingDialog *loadingDialog_;
    FindSuccessDialog *findSuccessDialog_ = nullptr;
};

#endif // SEARCHLIST_H
