#ifndef SEARCHLIST_H
#define SEARCHLIST_H

#include <QListWidget>
struct SearchInfo;
class SearchResultDialog;
class LoadingDialog;
class SearchList : public QListWidget {
    Q_OBJECT
public:
    explicit SearchList(QWidget *parent = nullptr);
    void CloseSearchResultDialog();
    void SetSearchEdit(QWidget *w);

signals:
    // 搜索命中已是好友的用户，跳转到与该好友的聊天页
    void SigJumpToChat(int uid);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
private slots:
    void OnItemClicked(QListWidgetItem *item);
    void OnUserSearch(std::shared_ptr<SearchInfo> info);

private:
    void SetSearchPending(bool pending = true); // 显示/隐藏搜索 loading 并记录进行中状态
    void AddSearchEntryItem();                 // 添加"查找 uid/name"入口条目
    bool searchPending_;
    QWidget *searchEdit_;
    LoadingDialog *loadingDialog_;
    SearchResultDialog *searchResultDialog_ = nullptr;
};

#endif // SEARCHLIST_H
