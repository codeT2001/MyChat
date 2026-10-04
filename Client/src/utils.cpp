#include "utils.h"
#include "log.h"
#include <QStyle>
#include <QApplication>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>

namespace {
// Server URLs
constexpr char SERVER_HOST[] = "81.69.247.52";
constexpr int SERVER_PORT = 9090;
} // namespace
QMap<TipType, QString> Utils::tips_;
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

void Utils::AddTip(QLabel *label, TipType type, const QString &tip)
{
    if (!label) {
        return;
    }
    tips_[type] = tip;
    ShowTip(label, tip, true);
}

void Utils::DeleteTip(QLabel *label, TipType type)
{

    if (!label) {
        return;
    }
    tips_.remove(type);
    if (tips_.empty()) {
        label->clear();
        return;
    }
    ShowTip(label, tips_.first(), true);
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
        AddTip(label, TipType::USER_ERR, tr("用户名长度应为%1~%2字符").arg(minLength).arg(maxLength));
        return false;
    }
    if (!IsUserNameValid(str)) {
        AddTip(label, TipType::USER_ERR, tr("用户名只能包含字母、数字和下划线_"));
        return false;
    }

    DeleteTip(label, TipType::USER_ERR);
    return true;
}

bool Utils::CheckPasswordValid(const QString &str, QLabel *label, int32_t minLength, int32_t maxLength)
{
    if (str.length() < minLength || str.length() > maxLength) {
        AddTip(label, TipType::PWD_ERR, tr("密码长度应为%1~%2字符").arg(minLength).arg(maxLength));
        return false;
    }

    if (!IsPasswordValid(str)) {
        Utils::AddTip(label, TipType::PWD_ERR, tr("密码只能包含字母、数字和特殊字符!@#$%^&*"));
        return false;
    }

    DeleteTip(label, TipType::PWD_ERR);
    return true;
}

bool Utils::CheckConfirmValid(const QString &pwd, const QString confirm, QLabel *label)
{
    if (pwd != confirm) {
        AddTip(label, TipType::CONFIRM_ERR, tr("两次输入的密码不匹配"));
        return false;
    }

    DeleteTip(label, TipType::CONFIRM_ERR);
    return true;
}

bool Utils::CheckEmailValid(const QString &str, QLabel *label)
{
    if (!Utils::IsEmailValid(str)) {
        Utils::AddTip(label, TipType::EMAIL_ERR, tr("邮箱地址格式不正确"));
        return false;
    }

    Utils::DeleteTip(label, TipType::EMAIL_ERR);
    return true;
}

bool Utils::CheckVerifyCodeValid(const QString &str, QLabel *label, int32_t length)
{
    if (str.length() != length) {
        Utils::AddTip(label, TipType::VERIFY_CODE_ERR, tr("验证码长度应为%1").arg(length));
        return false;
    }
    Utils::DeleteTip(label, TipType::VERIFY_CODE_ERR);
    return true;
}

QString Utils::GetServerUrl(const QString &path)
{
    return QString("http://%1:%2%3").arg(SERVER_HOST).arg(SERVER_PORT).arg(path);
}

void Utils::ClearTips()
{
    tips_.clear();
}
