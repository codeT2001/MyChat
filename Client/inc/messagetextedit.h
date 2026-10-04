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

    QVector<MsgInfo> GetMsgList();

    void InsertFileFromUrl(const QStringList &urls);
signals:
    void Send();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void TextEditChanged();

private:
    void InsertImages(const QString &url);
    void InsertTextFile(const QString &url);
    bool CanInsertFromMimeData(const QMimeData *source) const;
    void InsertFromMimeData(const QMimeData *source);
    bool IsImage(QString url); // 判断文件是否为图片
    void InsertMsgList(QVector<MsgInfo> &list, QString flag, QString text, QPixmap pix);

    QStringList GetUrl(QString text);
    QPixmap GetFileIconPixmap(const QString &url); // 获取文件图标及大小信息，并转化成图片
    QString GetFileSize(qint64 size);              // 获取文件大小
    QVector<MsgInfo> mMsgList;
    QVector<MsgInfo> mGetMsgList;
};

#endif // MESSAGETEXTEDIT_H
