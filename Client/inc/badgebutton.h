#ifndef BADGEBUTTON_H
#define BADGEBUTTON_H

#include <QPushButton>

class BadgeButton : public QPushButton {
    Q_OBJECT
public:
    explicit BadgeButton(QWidget *parent = nullptr);
    ~BadgeButton();

    // 显示/隐藏红点
    void SetShowBadge(bool show);
    bool IsBadgeShown() const;

    // (可选) 设置红点大小，默认 10
    void SetBadgeSize(int size);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool showBadge_;
    int badgeSize_;
};

#endif // BADGEBUTTON_H
