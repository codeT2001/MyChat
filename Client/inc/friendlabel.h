#ifndef FRIENDLABEL_H
#define FRIENDLABEL_H

#include <QFrame>
#include "qclicklabel.h"

namespace Ui {
class FriendLabel;
}

class FriendLabel : public QFrame {
    Q_OBJECT

public:
    explicit FriendLabel(QWidget *parent = nullptr);
    ~FriendLabel();
    void SetText(const QString &text);
    int32_t Width() const;
    int32_t Height() const;
    const QString &Text() const;
    bool IsSelected() const
    {
        return selected_;
    }
    void SetSelected(bool selected);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void UpdateStateStyle();
    Ui::FriendLabel *ui;
    QString text_;
    int32_t width_;
    int32_t height_;
    bool selected_;
signals:
    void SigToggle(const QString &text, ClickLabelState state);
};

#endif // FRIENDLABEL_H
