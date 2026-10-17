#include <QtTest>
#include <QDir>
#include <QRegularExpression>
#include <QSet>

#include "test_helpers.h"

// 资源登记契约（Client/AGENTS.md：新增样式或图片必须写进 rc.qrc）：
//   1. rc.qrc 里登记的每个文件都必须真实存在——否则 rcc 阶段构建直接失败；
//   2. images/ 与 style/ 下的每个文件都必须被登记——否则运行期 QSS/图片加载不到。
// 唯一的例外是 style/_design_tokens.qss（设计规范参考，不参与运行期加载）。

namespace {

const char *const kUnregisteredAllowed[] = {"style/_design_tokens.qss"};

QSet<QString> registeredFiles(const QString &qrcText)
{
    QSet<QString> files;
    const QRegularExpression re(QStringLiteral("<file>([^<]+)</file>"));
    auto it = re.globalMatch(qrcText);
    while (it.hasNext()) {
        files.insert(it.next().captured(1).trimmed());
    }
    return files;
}

QSet<QString> filesOnDisk(const QString &clientRoot)
{
    QSet<QString> files;
    for (const QString &dirName : {QStringLiteral("images"), QStringLiteral("style")}) {
        QDir dir(clientRoot + "/" + dirName);
        const QStringList names = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : names) {
            files.insert(dirName + "/" + name);
        }
    }
    return files;
}

} // namespace

class TestQrcAssets : public QObject
{
    Q_OBJECT

private slots:
    void qrcParsedNotEmpty();
    void everyRegisteredFileExists();
    void everyAssetFileIsRegistered();
};

void TestQrcAssets::qrcParsedNotEmpty()
{
    const QString qrc = testhelp::readClientFile("rc.qrc");
    QVERIFY2(!qrc.isEmpty(), "读不到 Client/rc.qrc（检查 CHAT_CLIENT_ROOT）");
    QVERIFY(registeredFiles(qrc).size() > 50); // 资源清单解析出来不应为空/残缺
}

void TestQrcAssets::everyRegisteredFileExists()
{
    const QString clientRoot = QString::fromUtf8(CHAT_CLIENT_ROOT);
    const QSet<QString> registered = registeredFiles(testhelp::readClientFile("rc.qrc"));
    QVERIFY(!registered.isEmpty());

    for (const QString &relative : registered) {
        QVERIFY2(QFile::exists(clientRoot + "/" + relative),
                 qPrintable(QString("rc.qrc 登记了不存在的文件：%1（rcc 构建会失败）").arg(relative)));
    }
}

void TestQrcAssets::everyAssetFileIsRegistered()
{
    const QSet<QString> registered = registeredFiles(testhelp::readClientFile("rc.qrc"));
    QSet<QString> allowed; // 明确豁免的文件
    for (const char *item : kUnregisteredAllowed) {
        allowed.insert(QString::fromLatin1(item));
    }

    const QSet<QString> onDisk = filesOnDisk(QString::fromUtf8(CHAT_CLIENT_ROOT));
    QVERIFY(!onDisk.isEmpty());

    for (const QString &relative : onDisk) {
        if (allowed.contains(relative)) {
            continue;
        }
        QVERIFY2(registered.contains(relative),
                 qPrintable(QString("%1 未登记到 rc.qrc（新增资源必须登记，否则运行期加载不到）").arg(relative)));
    }
}

QTEST_GUILESS_MAIN(TestQrcAssets)
#include "test_qrc_assets.moc"
