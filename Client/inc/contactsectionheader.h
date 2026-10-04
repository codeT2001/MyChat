#ifndef CONTACTSECTIONHEADER_H
#define CONTACTSECTIONHEADER_H

#include <QWidget>
#include "listitembase.h"
namespace Ui {
class ContactSectionHeader;
}

class ContactSectionHeader : public ListItemBase {
    Q_OBJECT

public:
    explicit ContactSectionHeader(QWidget *parent = nullptr);
    ~ContactSectionHeader();
    QSize sizeHint() const override;
    void SetGroupTip(const QString &str);

private:
    QString groupName_;
    Ui::ContactSectionHeader *ui;
};

#endif // CONTACTSECTIONHEADER_H
