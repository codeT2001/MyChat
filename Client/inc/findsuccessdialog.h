#ifndef FINDSUCCESSDIALOG_H
#define FINDSUCCESSDIALOG_H

#include <QDialog>

namespace Ui {
class FindSuccessDialog;
}
struct SearchInfo;
class FindSuccessDialog : public QDialog {
    Q_OBJECT

public:
    explicit FindSuccessDialog(bool success = true, QWidget *parent = nullptr);
    ~FindSuccessDialog();
    void SetSearchInfo(std::shared_ptr<SearchInfo> info);
private slots:
    void OnAddFriendBtnClicked();

private:
    Ui::FindSuccessDialog *ui;
    QWidget *parent_;
    std::shared_ptr<SearchInfo> info_;
};

#endif // FINDSUCCESSDIALOG_H
