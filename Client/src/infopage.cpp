#include "infopage.h"
#include "ui_infopage.h"
#include "userdata.h"
#include "usermanager.h"
#include "utils.h"
#include "log.h"
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QPixmap>
#include <QIcon>
#include <QAction>
#include <QLineEdit>
#include <QCursor>
#include <QEvent>
#include <QFont>

InfoPage::InfoPage(QWidget *parent) : QWidget(parent), ui(new Ui::InfoPage)
{
    ui->setupUi(this);
    // QWidget 子类必须开启此属性，QSS 的 background 才会生效
    setAttribute(Qt::WA_StyledBackground);
    Utils::LoadQss(this, ":/style/infopage.qss");

    // 昵称使用代码设置粗体字号（优先级高于 QSS，与项目其他页面一致）
    QFont nameFont = ui->nameLb->font();
    nameFont.setBold(true);
    nameFont.setPointSize(16);
    ui->nameLb->setFont(nameFont);

    // 备注/标签：始终可直接编辑；行尾挂编辑图标仅作"可编辑"提示，hover 行时显示
    AttachEditAction(ui->remarkEdit, remarkEditAction_, tr("修改备注"));
    AttachEditAction(ui->labelEdit, labelEditAction_, tr("修改标签"));

    // hover 行容器：显隐对应行尾图标
    ui->remarkRow->installEventFilter(this);
    ui->labelRow->installEventFilter(this);

    // 图标仅作提示，点击它时把焦点交给输入框（直接点文本任意位置也可编辑）
    connect(remarkEditAction_, &QAction::triggered, this, [this]() { ui->remarkEdit->setFocus(); });
    connect(labelEditAction_, &QAction::triggered, this, [this]() { ui->labelEdit->setFocus(); });
    // 回车或失焦 → 与基线值比对，有变化才提交
    connect(ui->remarkEdit, &QLineEdit::editingFinished, this, &InfoPage::CommitRemark);
    connect(ui->labelEdit, &QLineEdit::editingFinished, this, &InfoPage::CommitLabel);

    connect(ui->msgBtn, &QToolButton::clicked, this, [this]() { emit SigSendMessage(uid_); });
    connect(ui->voiceBtn, &QToolButton::clicked, this, [this]() { emit SigVoiceCall(uid_); });
    connect(ui->videoBtn, &QToolButton::clicked, this, [this]() { emit SigVideoCall(uid_); });
}

InfoPage::~InfoPage()
{
    delete ui;
}

void InfoPage::SetInfo(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        LOG_WARN() << "InfoPage::SetInfo info is nullptr";
        return;
    }
    FillContent(info->uid_, info->name_, info->nick_, info->icon_, info->label_);
    ui->sectionTitleLb->setText(tr("朋友资料"));
    SetEditableRows(true);
    SetActionsVisible(true);
}

void InfoPage::SetSelfInfo(std::shared_ptr<UserInfo> info)
{
    if (!info) {
        LOG_WARN() << "InfoPage::SetSelfInfo info is nullptr";
        return;
    }
    FillContent(info->uid_, info->name_, info->nick_, info->icon_, QString());
    ui->sectionTitleLb->setText(tr("个人资料"));
    SetEditableRows(false);
    SetActionsVisible(false);
}

void InfoPage::FillContent(int uid,
                           const QString &name,
                           const QString &nick,
                           const QString &icon,
                           const QString &label)
{
    uid_ = uid;

    // 头像：ResolveIcon 统一解析协议 icon 字段，圆形化展示
    ui->iconLb->setPixmap(Utils::RoundedAvatar(icon, ui->iconLb->size()));

    // 顶部大字显示昵称，昵称为空时回退用户名
    ui->nameLb->setText(nick.isEmpty() ? name : nick);

    // 记录已提交的基线值；文本框始终可编辑，空值由 placeholderText 呈现灰色占位
    remarkBase_ = name;
    labelBase_ = label;
    ui->remarkEdit->setText(name);
    ui->labelEdit->setText(label);
}

void InfoPage::SetActionsVisible(bool visible)
{
    ui->msgBtn->setVisible(visible);
    ui->voiceBtn->setVisible(visible);
    ui->videoBtn->setVisible(visible);
}

void InfoPage::SetEditableRows(bool editable)
{
    // 自己没有备注/标签的概念，整行隐藏
    ui->remarkRow->setVisible(editable);
    ui->labelRow->setVisible(editable);
    if (!editable) {
        remarkEditAction_->setVisible(false);
        labelEditAction_->setVisible(false);
    }
}

void InfoPage::AttachEditAction(QLineEdit *edit, QAction *&action, const QString &tip)
{
    action = edit->addAction(QIcon(":/images/edit.png"), QLineEdit::TrailingPosition);
    action->setToolTip(tip);
    action->setVisible(false); // 默认隐藏，hover 行时显示
}

void InfoPage::CommitEdit(QLineEdit *edit,
                          QString &baseText,
                          const std::function<void(const QString &)> &apply)
{
    const QString text = edit->text().trimmed();
    // 显示归一化（去首尾空格），与基线相同则不重复落库
    if (text != edit->text()) {
        edit->setText(text);
    }
    if (text == baseText) {
        return;
    }
    baseText = text;
    apply(text);
}

bool InfoPage::eventFilter(QObject *watched, QEvent *event)
{
    auto handleHover = [](QObject *container, QAction *action, QEvent *e) {
        if (e->type() == QEvent::Enter) {
            action->setVisible(true);
            return false;
        }
        if (e->type() == QEvent::Leave) {
            // 移入行内子控件时父容器也会收到 Leave，鼠标仍在行内则不隐藏
            auto *w = qobject_cast<QWidget *>(container);
            if (w && !w->rect().contains(w->mapFromGlobal(QCursor::pos()))) {
                action->setVisible(false);
            }
            return false;
        }
        return false;
    };

    if (watched == ui->remarkRow) {
        return handleHover(ui->remarkRow, remarkEditAction_, event);
    }
    if (watched == ui->labelRow) {
        return handleHover(ui->labelRow, labelEditAction_, event);
    }
    return QWidget::eventFilter(watched, event);
}

void InfoPage::CommitRemark()
{
    CommitEdit(ui->remarkEdit, remarkBase_, [this](const QString &text) {
        // 更新本地好友备注（服务端备注同步协议待接入）
        UserManager::GetInstance().UpdateFriendRemark(uid_, text);
        LOG_INFO() << "friend remark updated, uid =" << uid_ << " remark =" << text;
    });
}

void InfoPage::CommitLabel()
{
    CommitEdit(ui->labelEdit, labelBase_, [this](const QString &text) {
        // 更新本地好友标签（服务端标签同步协议待接入）
        UserManager::GetInstance().UpdateFriendLabel(uid_, text);
        LOG_INFO() << "friend label updated, uid =" << uid_ << " label =" << text;
    });
}

void InfoPage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
