#ifndef SEARCHRESULTDIALOG_H
#define SEARCHRESULTDIALOG_H

#include <QDialog>

namespace Ui {
class SearchResultDialog;
}
struct SearchInfo;
class SearchResultDialog : public QDialog {
    Q_OBJECT

public:
    explicit SearchResultDialog(bool success = true, QWidget *parent = nullptr);
    ~SearchResultDialog();
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
private slots:
    void OnAddFriendBtnClicked();

private:
    Ui::SearchResultDialog *ui;
    QWidget *parent_;
    std::shared_ptr<SearchInfo> info_;
};

#endif // SEARCHRESULTDIALOG_H
