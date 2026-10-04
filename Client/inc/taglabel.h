#ifndef TAGLABEL_H
#define TAGLABEL_H

#include <QFrame>
#include "statefulclicklabel.h"

namespace Ui {
class TagLabel;
}

// 可点击切换选中态的标签（用于好友分组/备注标签选择）
class TagLabel : public QFrame {
    Q_OBJECT

public:
    explicit TagLabel(QWidget *parent = nullptr);
    ~TagLabel();
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
    Ui::TagLabel *ui;
    QString text_;
    int32_t width_;
    int32_t height_;
    bool selected_;
signals:
    void SigToggle(const QString &text, ClickLabelState state);
};

#endif // TAGLABEL_H
