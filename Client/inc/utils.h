#ifndef UTILS_H
#define UTILS_H

#include <QWidget>
#include <QLabel>
#include <QObject>
#include <QPixmap>
#include <QSize>

enum class TipType {
    SUCCESS = 0,
    EMAIL_ERR = 1,
    PWD_ERR = 2,
    CONFIRM_ERR = 3,
    VERIFY_CODE_ERR = 5,
    USER_ERR = 6
};

constexpr int32_t USERNAME_MIN_LENGTH = 3;
constexpr int32_t USERNAME_MAX_LENGTH = 20;
constexpr int32_t PASSWORD_MIN_LENGTH = 6;
constexpr int32_t PASSWORD_MAX_LENGTH = 15;
constexpr int32_t VERIFY_CODE_LENGTH = 4;

class Utils : public QObject {
    Q_OBJECT
public:
    static void ShowTip(QLabel *tip, const QString &msg, bool isError = false);
    static void RefreshWidgetStyle(QWidget *w);
    // 从 qrc 加载 qss 并应用到指定 widget（作用域：该 widget 及其子控件）
    static void LoadQss(QWidget *w, const QString &qrcPath);

    // Validation functions
    static bool IsEmailValid(const QString &email);
    static bool IsPasswordValid(const QString &pwd);
    static bool IsUserNameValid(const QString &username);
    static void PushTip(QLabel *label, TipType type, const QString &tip);
    static void PopTip(QLabel *label, TipType type);
    static bool CheckUserValid(const QString &str,
                               QLabel *label = nullptr,
                               int32_t minLength = USERNAME_MIN_LENGTH,
                               int32_t maxLength = USERNAME_MAX_LENGTH);
    static bool CheckPasswordValid(const QString &str,
                                   QLabel *label = nullptr,
                                   int32_t minLength = PASSWORD_MIN_LENGTH,
                                   int32_t maxLength = PASSWORD_MAX_LENGTH);
    static bool CheckConfirmValid(const QString &pwd, const QString confirm, QLabel *label = nullptr);
    static bool CheckEmailValid(const QString &str, QLabel *label = nullptr);
    static bool CheckVerifyCodeValid(const QString &str, QLabel *label = nullptr, int32_t length = VERIFY_CODE_LENGTH);
    static QString GetServerUrl(const QString &path);
    static void ClearTips();

    // 服务端 icon 字段 -> 本地头像资源路径（唯一入口）。
    // 约定："head_N" -> ":/images/head_N.jpg"（N=1..kAvatarCount）；
    // 空串/历史占位("icon")/非法值 -> 默认头像；已是 qrc 路径(":/...")原样透传
    static QString ResolveIcon(const QString &icon);

    // 头像圆形化：按短边裁成正方形后切圆，输出带透明通道。
    // QSS 的 border-radius 无法裁剪 QLabel 上的 pixmap，圆形头像需在此处理
    static QPixmap RoundedAvatar(const QString &icon, const QSize &size);

private:
    Utils() = default;
    ~Utils() = default;
    static QMap<TipType, QString> tipStack_;
};

#endif // UTILS_H
