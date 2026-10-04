#include "messagetextedit.h"
#include "logger.h"
#include <QDebug>
#include <QMessageBox>

MessageTextEdit::MessageTextEdit(QWidget *parent) : QTextEdit(parent)
{
    this->setMaximumHeight(60);
}

MessageTextEdit::~MessageTextEdit() {}

QVector<MsgInfo> MessageTextEdit::TakeMsgList()
{
    QVector<MsgInfo> result;

    QString doc = this->document()->toPlainText();
    QString text = ""; // 存储文本信息
    int indexUrl = 0;
    int count = attachedMsgs_.size();

    for (int index = 0; index < doc.size(); index++) {
        if (doc[index] == QChar::ObjectReplacementCharacter) {
            if (!text.isEmpty()) {
                QPixmap pix;
                AppendMsgInfo(result, "text", text, pix);
                text.clear();
            }
            while (indexUrl < count) {
                MsgInfo msg = attachedMsgs_[indexUrl];
                if (this->document()->toHtml().contains(msg.content, Qt::CaseSensitive)) {
                    indexUrl++;
                    result.append(msg);
                    break;
                }
                indexUrl++;
            }
        } else {
            text.append(doc[index]);
        }
    }
    if (!text.isEmpty()) {
        QPixmap pix;
        AppendMsgInfo(result, "text", text, pix);
        text.clear();
    }
    attachedMsgs_.clear();
    this->clear();
    return result;
}

void MessageTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this)
        event->ignore();
    else
        event->accept();
}

void MessageTextEdit::dropEvent(QDropEvent *event)
{
    InsertFromMimeData(event->mimeData());
    event->accept();
}

void MessageTextEdit::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) && !(e->modifiers() & Qt::ShiftModifier)) {
        emit SigSend();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void MessageTextEdit::InsertFileFromUrl(const QStringList &urls)
{
    if (urls.isEmpty())
        return;

    foreach (QString url, urls) {
        if (IsImageUrl(url)) {
            InsertImage(url);
        } else {
            InsertTextFile(url);
        }
    }
}

void MessageTextEdit::InsertImage(const QString &url)
{
    QImage image(url);
    if (image.isNull()) {
        LOG_WARN() << "Error: Failed to load image from" << url;
        return;
    }
    // 按比例缩放图片
    if (image.width() > 120 || image.height() > 80) {
        if (image.width() > image.height()) {
            image = image.scaledToWidth(120, Qt::SmoothTransformation);
        } else {
            image = image.scaledToHeight(80, Qt::SmoothTransformation);
        }
    }
    QTextCursor cursor = this->textCursor();
    cursor.insertImage(image, url);

    AppendMsgInfo(attachedMsgs_, "image", url, QPixmap::fromImage(image));
}

void MessageTextEdit::InsertTextFile(const QString &url)
{
    QFileInfo fileInfo(url);
    if (fileInfo.isDir()) {
        QMessageBox::information(this, "提示", "只允许拖拽单个文件!");
        return;
    }

    if (fileInfo.size() > 100 * 1024 * 1024) {
        QMessageBox::information(this, "提示", "发送的文件大小不能大于100M");
        return;
    }

    QPixmap pix = GetFileIconPixmap(url);
    QTextCursor cursor = this->textCursor();
    cursor.insertImage(pix.toImage(), url);
    AppendMsgInfo(attachedMsgs_, "file", url, pix);
}

void MessageTextEdit::InsertFromMimeData(const QMimeData *source)
{
    QStringList urls = ExtractFileUrls(source->text());

    if (!urls.isEmpty()) {
        foreach (const QString &url, urls) {
            if (IsImageUrl(url)) {
                InsertImage(url);
            } else {
                InsertTextFile(url);
            }
        }
    } else if (source->hasText()) {
        QTextEdit::insertFromMimeData(source);
    }
}

bool MessageTextEdit::IsImageUrl(const QString &url) const
{
    static const QStringList imageFormats =
        {"bmp", "jpg", "png", "tif", "gif", "pcx", "tga", "exif", "fpx", "svg",
         "psd", "cdr", "pcd", "dxf", "ufo", "eps", "ai", "raw", "wmf", "webp"};
    QFileInfo fileInfo(url);
    return imageFormats.contains(fileInfo.suffix(), Qt::CaseInsensitive);
}

void MessageTextEdit::AppendMsgInfo(QVector<MsgInfo> &list,
                                    const QString &flag,
                                    const QString &text,
                                    const QPixmap &pix)
{
    MsgInfo msg;
    msg.msgFlag = flag;
    msg.content = text;
    msg.pixmap = pix;
    list.append(msg);
}

QStringList MessageTextEdit::ExtractFileUrls(const QString &text)
{
    QStringList urls;
    if (text.isEmpty())
        return urls;

    QStringList list = text.split("\n");
    foreach (QString url, list) {
        if (!url.isEmpty()) {
            QStringList str = url.split("///");
            if (str.size() >= 2)
                urls.append(str.at(1));
        }
    }
    return urls;
}

QPixmap MessageTextEdit::GetFileIconPixmap(const QString &url)
{
    QFileIconProvider provider;
    QFileInfo fileinfo(url);
    QIcon icon = provider.icon(fileinfo);

    QString strFileSize = FormatFileSize(fileinfo.size());

    QFont font(QString("宋体"), 10, QFont::Normal, false);
    QFontMetrics fontMetrics(font);
    QSize textSize = fontMetrics.size(Qt::TextSingleLine, fileinfo.fileName());

    QSize fileSizeText = fontMetrics.size(Qt::TextSingleLine, strFileSize);
    int maxWidth = textSize.width() > fileSizeText.width() ? textSize.width() : fileSizeText.width();
    QPixmap pix(50 + maxWidth + 10, 50);
    pix.fill();

    QPainter painter;
    painter.begin(&pix);
    // 文件图标
    QRect rect(0, 0, 50, 50);
    painter.drawPixmap(rect, icon.pixmap(40, 40));
    painter.setPen(Qt::black);
    // 文件名称
    QRect rectText(50 + 10, 3, textSize.width(), textSize.height());
    painter.drawText(rectText, fileinfo.fileName());
    // 文件大小
    QRect rectFile(50 + 10, textSize.height() + 5, fileSizeText.width(), fileSizeText.height());
    painter.drawText(rectFile, strFileSize);
    painter.end();
    return pix;
}

QString MessageTextEdit::FormatFileSize(qint64 size)
{
    QString unit;
    double num;
    if (size < 1024) {
        num = size;
        unit = "B";
    } else if (size < 1024 * 1224) {
        num = size / 1024.0;
        unit = "KB";
    } else if (size < 1024LL * 1024 * 1024) {
        num = size / 1024.0 / 1024.0;
        unit = "MB";
    } else {
        num = size / 1024.0 / 1024.0 / 1024.0;
        unit = "GB";
    }
    return QString::number(num, 'f', 2) + " " + unit;
}
