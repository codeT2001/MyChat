#include "messagetextedit.h"
#include "log.h"
#include <QDebug>
#include <QMessageBox>

MessageTextEdit::MessageTextEdit(QWidget *parent) : QTextEdit(parent)
{

    // this->setStyleSheet("border: none;");
    this->setMaximumHeight(60);

    //    connect(this,SIGNAL(textChanged()),this,SLOT(textEditChanged()));
}

MessageTextEdit::~MessageTextEdit() {}

QVector<MsgInfo> MessageTextEdit::GetMsgList()
{
    mGetMsgList.clear();

    QString doc = this->document()->toPlainText();
    QString text = ""; // 存储文本信息
    int indexUrl = 0;
    int count = mMsgList.size();

    for (int index = 0; index < doc.size(); index++) {
        if (doc[index] == QChar::ObjectReplacementCharacter) {
            if (!text.isEmpty()) {
                QPixmap pix;
                InsertMsgList(mGetMsgList, "text", text, pix);
                text.clear();
            }
            while (indexUrl < count) {
                MsgInfo msg = mMsgList[indexUrl];
                if (this->document()->toHtml().contains(msg.content, Qt::CaseSensitive)) {
                    indexUrl++;
                    mGetMsgList.append(msg);
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
        InsertMsgList(mGetMsgList, "text", text, pix);
        text.clear();
    }
    mMsgList.clear();
    this->clear();
    return mGetMsgList;
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
        emit Send();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void MessageTextEdit::InsertFileFromUrl(const QStringList &urls)
{
    if (urls.isEmpty())
        return;

    foreach (QString url, urls) {
        if (IsImage(url)) {
            InsertImages(url);
        } else {
            InsertTextFile(url);
        }
    }
}

void MessageTextEdit::InsertImages(const QString &url)
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
    // QTextDocument *document = this->document();
    // document->addResource(QTextDocument::ImageResource, QUrl(url), QVariant(image));
    cursor.insertImage(image, url);

    InsertMsgList(mMsgList, "image", url, QPixmap::fromImage(image));
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
    InsertMsgList(mMsgList, "file", url, pix);
}

bool MessageTextEdit::CanInsertFromMimeData(const QMimeData *source) const
{
    return QTextEdit::canInsertFromMimeData(source);
}

void MessageTextEdit::InsertFromMimeData(const QMimeData *source)
{
    QStringList urls = GetUrl(source->text());

    if (!urls.isEmpty()) {
        foreach (const QString &url, urls) {
            if (IsImage(url)) {
                InsertImages(url);
            } else {
                InsertTextFile(url);
            }
        }
    } else if (source->hasText()) {
        QTextEdit::insertFromMimeData(source);
    }
}

bool MessageTextEdit::IsImage(QString url)
{
    LOG_DEBUG() << "IsImage INTO";
    QString imageFormat = "bmp,jpg,png,tif,gif,pcx,tga,exif,fpx,svg,psd,cdr,pcd,dxf,ufo,eps,ai,raw,wmf,webp";
    QStringList imageFormatList = imageFormat.split(",");
    QFileInfo fileInfo(url);
    QString suffix = fileInfo.suffix();
    if (imageFormatList.contains(suffix, Qt::CaseInsensitive)) {
        return true;
    }
    return false;
}

void MessageTextEdit::InsertMsgList(QVector<MsgInfo> &list, QString flag, QString text, QPixmap pix)
{
    MsgInfo msg;
    msg.msgFlag = flag;
    msg.content = text;
    msg.pixmap = pix;
    list.append(msg);
}

QStringList MessageTextEdit::GetUrl(QString text)
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
    QFileIconProvider provder;
    QFileInfo fileinfo(url);
    QIcon icon = provder.icon(fileinfo);

    QString strFileSize = GetFileSize(fileinfo.size());
    // qDebug() << "FileSize=" << fileinfo.size();

    QFont font(QString("宋体"), 10, QFont::Normal, false);
    QFontMetrics fontMetrics(font);
    QSize textSize = fontMetrics.size(Qt::TextSingleLine, fileinfo.fileName());

    QSize FileSize = fontMetrics.size(Qt::TextSingleLine, strFileSize);
    int maxWidth = textSize.width() > FileSize.width() ? textSize.width() : FileSize.width();
    QPixmap pix(50 + maxWidth + 10, 50);
    pix.fill();

    QPainter painter;
    // painter.setRenderHint(QPainter::Antialiasing, true);
    // painter.setFont(font);
    painter.begin(&pix);
    // 文件图标
    QRect rect(0, 0, 50, 50);
    painter.drawPixmap(rect, icon.pixmap(40, 40));
    painter.setPen(Qt::black);
    // 文件名称
    QRect rectText(50 + 10, 3, textSize.width(), textSize.height());
    painter.drawText(rectText, fileinfo.fileName());
    // 文件大小
    QRect rectFile(50 + 10, textSize.height() + 5, FileSize.width(), FileSize.height());
    painter.drawText(rectFile, strFileSize);
    painter.end();
    return pix;
}

QString MessageTextEdit::GetFileSize(qint64 size)
{
    QString Unit;
    double num;
    if (size < 1024) {
        num = size;
        Unit = "B";
    } else if (size < 1024 * 1224) {
        num = size / 1024.0;
        Unit = "KB";
    } else if (size < 1024 * 1024 * 1024) {
        num = size / 1024.0 / 1024.0;
        Unit = "MB";
    } else {
        num = size / 1024.0 / 1024.0 / 1024.0;
        Unit = "GB";
    }
    return QString::number(num, 'f', 2) + " " + Unit;
}

void MessageTextEdit::TextEditChanged()
{
    // qDebug() << "text changed!" << endl;
}
