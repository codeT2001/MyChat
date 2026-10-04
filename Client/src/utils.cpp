#include "utils.h"
#include "logger.h"
#include <QStyle>
#include <QApplication>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QPainter>
#include <QPainterPath>
#include <cstring>

namespace {
// Server URLs
constexpr char SERVER_HOST[] = "81.69.247.52";
constexpr int SERVER_PORT = 9090;
} // namespace
QMap<TipType, QString> Utils::tipStack_;
void Utils::ShowTip(QLabel *tip, const QString &msg, bool isError)
{
    if (!tip) {
        return;
    }
    tip->setText(msg);
    QPalette pal = tip->palette();
    if (isError) {
        pal.setColor(QPalette::WindowText, Qt::red);
    } else {
        pal.setColor(QPalette::WindowText, Qt::green);
    }
    tip->setPalette(pal);
    tip->setAutoFillBackground(true);
}

void Utils::RefreshWidgetStyle(QWidget *w)
{
    if (!w) {
        return;
    }
    QStyle *style = w->style();
    style->unpolish(w);
    style->polish(w);
    w->update();
}

void Utils::LoadQss(QWidget *w, const QString &qrcPath)
{
    if (!w) {
        return;
    }
    QFile file(qrcPath);
    if (!file.open(QFile::ReadOnly)) {
        LOG_WARN() << "LoadQss: open failed:" << qrcPath;
        return;
    }
    // QTextStream 按 UTF-8 解码并自动跳过 BOM，避免 BOM 导致 QSS 解析失败
    QTextStream ts(&file);
    w->setStyleSheet(ts.readAll());
}

void Utils::PushTip(QLabel *label, TipType type, const QString &tip)
{
    if (!label) {
        return;
    }
    tipStack_[type] = tip;
    ShowTip(label, tip, true);
}

void Utils::PopTip(QLabel *label, TipType type)
{

    if (!label) {
        return;
    }
    tipStack_.remove(type);
    if (tipStack_.empty()) {
        label->clear();
        return;
    }
    ShowTip(label, tipStack_.first(), true);
}

bool Utils::IsEmailValid(const QString &email)
{
    static const QRegularExpression emailRegex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    return emailRegex.match(email).hasMatch();
}

bool Utils::IsPasswordValid(const QString &pwd)
{
    static const QRegularExpression passwordRegex("^[a-zA-Z0-9!@#$%^&*]+$");
    return passwordRegex.match(pwd).hasMatch();
}

bool Utils::IsUserNameValid(const QString &username)
{
    static const QRegularExpression userNameRegex("^[a-zA-Z0-9_]+$");
    return userNameRegex.match(username).hasMatch();
}

bool Utils::CheckUserValid(const QString &str, QLabel *label, int32_t minLength, int32_t maxLength)
{
    if (str.length() < minLength || str.length() > maxLength) {
        PushTip(label, TipType::USER_ERR, tr("用户名长度应为%1~%2字符").arg(minLength).arg(maxLength));
        return false;
    }
    if (!IsUserNameValid(str)) {
        PushTip(label, TipType::USER_ERR, tr("用户名只能包含字母、数字和下划线_"));
        return false;
    }

    PopTip(label, TipType::USER_ERR);
    return true;
}

bool Utils::CheckPasswordValid(const QString &str, QLabel *label, int32_t minLength, int32_t maxLength)
{
    if (str.length() < minLength || str.length() > maxLength) {
        PushTip(label, TipType::PWD_ERR, tr("密码长度应为%1~%2字符").arg(minLength).arg(maxLength));
        return false;
    }

    if (!IsPasswordValid(str)) {
        Utils::PushTip(label, TipType::PWD_ERR, tr("密码只能包含字母、数字和特殊字符!@#$%^&*"));
        return false;
    }

    PopTip(label, TipType::PWD_ERR);
    return true;
}

bool Utils::CheckConfirmValid(const QString &pwd, const QString confirm, QLabel *label)
{
    if (pwd != confirm) {
        PushTip(label, TipType::CONFIRM_ERR, tr("两次输入的密码不匹配"));
        return false;
    }

    PopTip(label, TipType::CONFIRM_ERR);
    return true;
}

bool Utils::CheckEmailValid(const QString &str, QLabel *label)
{
    if (!Utils::IsEmailValid(str)) {
        Utils::PushTip(label, TipType::EMAIL_ERR, tr("邮箱地址格式不正确"));
        return false;
    }

    Utils::PopTip(label, TipType::EMAIL_ERR);
    return true;
}

bool Utils::CheckVerifyCodeValid(const QString &str, QLabel *label, int32_t length)
{
    if (str.length() != length) {
        Utils::PushTip(label, TipType::VERIFY_CODE_ERR, tr("验证码长度应为%1").arg(length));
        return false;
    }
    Utils::PopTip(label, TipType::VERIFY_CODE_ERR);
    return true;
}

QString Utils::GetServerUrl(const QString &path)
{
    return QString("http://%1:%2%3").arg(SERVER_HOST).arg(SERVER_PORT).arg(path);
}

void Utils::ClearTips()
{
    tipStack_.clear();
}

namespace {
// 内置头像数量（images/head_1.jpg ~ head_N.jpg），与服务端注册分配范围保持一致
constexpr int kAvatarCount = 5;
constexpr char kDefaultIcon[] = ":/images/head_1.jpg";
} // namespace

QString Utils::ResolveIcon(const QString &icon)
{
    // 已是本地 qrc 路径，直接透传（如 "新的朋友" 用 add_friend.png）
    if (icon.startsWith(QLatin1String(":/"))) {
        return icon;
    }
    // 协议约定：head_N
    if (icon.startsWith(QLatin1String("head_"))) {
        bool ok = false;
        const int idx = icon.mid(strlen("head_")).toInt(&ok);
        if (ok && idx >= 1 && idx <= kAvatarCount) {
            return QStringLiteral(":/images/head_%1.jpg").arg(idx);
        }
    }
    // 空串、历史占位 "icon" 或非法值统一回退默认头像
    return QString::fromLatin1(kDefaultIcon);
}

QPixmap Utils::RoundedAvatar(const QString &icon, const QSize &size)
{
    QPixmap src(ResolveIcon(icon));
    if (src.isNull() || !size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return src;
    }
    // 先等比缩放并居中裁成目标比例的正方形，再切圆
    QPixmap square = src.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int side = qMin(square.width(), square.height());
    square = square.copy((square.width() - side) / 2, (square.height() - side) / 2, side, side)
                 .scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap out(size);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addEllipse(0, 0, size.width(), size.height());
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, square);
    return out;
}
