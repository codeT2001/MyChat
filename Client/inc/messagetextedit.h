#ifndef MESSAGETEXTEDIT_H
#define MESSAGETEXTEDIT_H

#include <QObject>
#include <QTextEdit>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMimeType>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QPainter>
#include <QVector>
#include "constants.h"
class MessageTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit MessageTextEdit(QWidget *parent = nullptr);
    ~MessageTextEdit();

    // 取出编辑框内按排版顺序排列的全部消息片段（文本/图片/文件），
    // 取出后清空输入框与附件记录（take 语义）
    QVector<MsgInfo> TakeMsgList();

    void InsertFileFromUrl(const QStringList &urls);
signals:
    void SigSend();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void InsertImage(const QString &url);
    void InsertTextFile(const QString &url);
    void InsertFromMimeData(const QMimeData *source);
    bool IsImageUrl(const QString &url) const; // 按后缀判断文件是否为图片
    void AppendMsgInfo(QVector<MsgInfo> &list, const QString &flag, const QString &text, const QPixmap &pix);

    QStringList ExtractFileUrls(const QString &text);
    QPixmap GetFileIconPixmap(const QString &url); // 生成文件图标缩略图（图标+文件名+大小）
    QString FormatFileSize(qint64 size);           // 字节数格式化为可读大小（B/KB/MB/GB）

    // 编辑过程中已插入输入框的图片/文件附件（文本在发送时从文档中提取）
    QVector<MsgInfo> attachedMsgs_;
};

#endif // MESSAGETEXTEDIT_H
