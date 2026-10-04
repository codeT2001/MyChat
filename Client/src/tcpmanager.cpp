#include "tcpmanager.h"
#include "logger.h"
namespace {
constexpr uint32_t MSG_HEAD_LENGTH = sizeof(uint16_t) * 2; // msgId + msgLength
constexpr int MAX_RECV_BUFFER_SIZE = 1024 * 1024;          // 接收缓冲区上限 1MB
constexpr int MAX_MSG_BODY_SIZE = 10 * 1024 * 1024;        // 单条消息体上限 10MB
} // namespace
TcpManager::TcpManager()
    : host_(""),
      port_(0),
      recvPending_(false),
      msgId_(0),
      msgLength_(0),
      heartbeatTimer_(new QTimer(this)),
      reconnectTimer_(new QTimer(this)),
      reconnectCount_(0),
      autoReconnect_(true)
{
    // 心跳定时器：发 PING 保活
    heartbeatTimer_->setInterval(TcpConfig::HEARTBEAT_INTERVAL_MS);
    connect(heartbeatTimer_, &QTimer::timeout, this, &TcpManager::OnHeartbeat);

    // 重连定时器：退避策略重连
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &TcpManager::TryReconnect);

    connect(&socket_, &QTcpSocket::connected, this, [this]() {
        LOG_INFO() << "connected to server!";
        ResetReconnectState();
        StartHeartbeat();
        emit SigConnected();
    });

    connect(&socket_, &QTcpSocket::readyRead, this, [this]() {
        buffer_.append(socket_.readAll());

        // 防止恶意协议包或服务器 bug 导致 buffer 无限增长
        if (buffer_.size() > MAX_RECV_BUFFER_SIZE) {
            LOG_WARN() << "TcpManager: recv buffer overflow (" << buffer_.size() << "bytes), disconnecting";
            socket_.abort();
            return;
        }

        forever
        {
            QDataStream stream(&buffer_, QIODevice::ReadOnly);
            stream.setVersion(QDataStream::Qt_6_0);
            if (!recvPending_) {
                if (buffer_.size() < static_cast<int>(MSG_HEAD_LENGTH)) {
                    return;
                }

                stream >> msgId_ >> msgLength_;

                buffer_ = buffer_.mid(MSG_HEAD_LENGTH);

                // 协议合理性校验：单条消息不能超过 MAX_MSG_BODY_SIZE
                if (msgLength_ > MAX_MSG_BODY_SIZE) {
                    LOG_WARN() << "TcpManager: invalid msgLength_=" << msgLength_ << ", disconnecting";
                    socket_.abort();
                    return;
                }

                LOG_DEBUG() << "message id : " << msgId_ << ", message length : " << msgLength_;
            }

            if (buffer_.size() < msgLength_) {
                recvPending_ = true;
                return;
            }

            recvPending_ = false;
            QByteArray body = buffer_.mid(0, msgLength_);
            LOG_DEBUG() << "recive message body is : " << body;
            HandleMsg(static_cast<RequestId>(msgId_), msgLength_, body);
            buffer_ = buffer_.mid(msgLength_);
        }
    });

    connect(&socket_, &QTcpSocket::errorOccurred, this, [&](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError)
        // 错误日志，重连逻辑由 disconnected 信号统一处理
        LOG_WARN() << "socket error:" << socket_.errorString();
    });
    connect(&socket_, &QTcpSocket::disconnected, this, [this]() {
        LOG_INFO() << "socket disconnected from server";
        OnSocketDisconnected();
    });
}

void TcpManager::Connect(ServerInfo info)
{

    QAbstractSocket::SocketState state = socket_.state();

    if (state != QAbstractSocket::UnconnectedState) {
        LOG_WARN() << "Connection already in progress or connected. State:" << state;
        return;
    }

    host_ = info.host;
    port_ = info.port;
    socket_.connectToHost(host_, port_);
}

void TcpManager::Send(RequestId id, const QByteArray &data)
{
    LOG_DEBUG() << "msgId : " << static_cast<uint32_t>(id) << "send data : " << QString(data);
    uint16_t copyId = static_cast<uint16_t>(id);
    uint16_t len = static_cast<uint16_t>(data.size());
    QByteArray block;
    block.reserve(MSG_HEAD_LENGTH + data.size());
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << copyId << len;
    block.append(data);
    qint64 bytesWritten = socket_.write(block);
    if (bytesWritten == -1) {
        LOG_WARN() << "TcpManager: socket write failed:" << socket_.errorString()
                   << ", remaining unsent:" << block.size();
    } else if (bytesWritten != block.size()) {
        // 写了一部分但没写完（发送窗口满了），Qt 会缓冲剩余数据
        LOG_DEBUG() << "TcpManager: partial write:" << bytesWritten << "/" << block.size();
    }
}

void TcpManager::HandleMsg(RequestId id, int32_t len, const QByteArray &data)
{
    Q_UNUSED(len)
    if (id == RequestId::CHAT_HEARTBEAT) {
        // 心跳响应（PONG），收到即说明连接存活，仅记录日志
        LOG_DEBUG() << "heartbeat pong received";
        return;
    }
    // 将原始报文转发给业务层（Service），由 Service 负责 JSON 解析与业务处理
    emit SigMessageReceived(id, data);
}

void TcpManager::Disconnect()
{
    LOG_INFO() << "TcpManager::Disconnect — user initiated";
    autoReconnect_ = false; // 阻止自动重连
    StopHeartbeat();
    reconnectTimer_->stop();
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort(); // 立即断开，不走优雅关闭
    }
}

void TcpManager::StartHeartbeat()
{
    heartbeatTimer_->start();
}

void TcpManager::StopHeartbeat()
{
    heartbeatTimer_->stop();
}

void TcpManager::OnHeartbeat()
{
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    LOG_DEBUG() << "TcpManager::OnHeartbeat — sending PING";
    Send(RequestId::CHAT_HEARTBEAT, QByteArray());
}

void TcpManager::OnSocketDisconnected()
{
    StopHeartbeat();
    buffer_.clear();
    recvPending_ = false;

    if (!autoReconnect_) {
        LOG_INFO() << "TcpManager::OnSocketDisconnected — autoReconnect disabled, giving up";
        return;
    }

    if (reconnectCount_ >= TcpConfig::MAX_RECONNECT_COUNT) {
        LOG_WARN() << "TcpManager: reconnect exceeded max count, giving up";
        autoReconnect_ = false;
        emit SigReconnectFailed();
        return;
    }

    TryReconnect();
}

void TcpManager::TryReconnect()
{
    if (host_.isEmpty() || port_ == 0) {
        LOG_WARN() << "TcpManager: no host/port for reconnect";
        return;
    }

    reconnectCount_++;
    // 退避策略：1s → 2s → 4s → ... → 60s
    int delay = TcpConfig::INITIAL_RECONNECT_DELAY_MS * (1 << (reconnectCount_ - 1));
    delay = qMin(delay, TcpConfig::MAX_RECONNECT_DELAY_MS);

    LOG_INFO() << "TcpManager: reconnect attempt" << reconnectCount_ << "/" << TcpConfig::MAX_RECONNECT_COUNT << "in"
               << delay << "ms...";

    if (reconnectCount_ <= TcpConfig::MAX_RECONNECT_COUNT) {
        reconnectTimer_->start(delay);
    }

    socket_.connectToHost(host_, port_);
}

void TcpManager::ResetReconnectState()
{
    reconnectCount_ = 0;
    autoReconnect_ = true;
    reconnectTimer_->stop();
}
