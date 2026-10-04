#ifndef LOADINGDIALOG_H
#define LOADINGDIALOG_H

#include <QDialog>
class QMovie;
namespace Ui {
class LoadingDialog;
}

class LoadingDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoadingDialog(QWidget *parent = nullptr);
    ~LoadingDialog();
    void Start();
    void Stop();

private:
    Ui::LoadingDialog *ui;
    QMovie *movie_;
};

#endif // LOADINGDIALOG_H
