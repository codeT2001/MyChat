#ifndef TCPMANAGER_H
#define TCPMANAGER_H
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include "constants.h"
#include "noncopyable.h"

// 连接生命周期相关常量
namespace TcpConfig {
constexpr int HEARTBEAT_INTERVAL_MS = 30000;     // 心跳间隔 30s
constexpr int INITIAL_RECONNECT_DELAY_MS = 1000; // 初始重连等待 1s
constexpr int MAX_RECONNECT_DELAY_MS = 60000;    // 最大重连等待 60s
constexpr int MAX_RECONNECT_COUNT = 10;          // 最大重连次数
} // namespace TcpConfig

class TcpManager : public QObject {
    Q_OBJECT
public:
    static TcpManager &GetInstance()
    {
        static TcpManager instance;
        return instance;
    }
    TcpManager();
    ~TcpManager() = default;

    // 主动断开连接（用户退出时调用，阻止自动重连）
    void Disconnect();

    // 连接指定聊天服务器（未连接状态下生效）
    void Connect(ServerInfo serverInfo);
    // 发送一条完整报文（自动加 msgId+length 包头）
    void Send(RequestId id, const QByteArray &data);

signals:
    void SigConnected();
    void SigReconnectFailed(); // 重连彻底失败（超过最大次数）
    // 收到完整报文后向业务层转发原始数据，由 Service 解析
    void SigMessageReceived(RequestId id, const QByteArray &data);

private:
    DISALLOW_COPY_MOVE(TcpManager)
    void HandleMsg(RequestId id, int32_t len, const QByteArray &data);
    // 连接生命周期管理
    void StartHeartbeat();
    void StopHeartbeat();
    void OnHeartbeat();
    void TryReconnect();
    void OnSocketDisconnected();
    void ResetReconnectState();

    QTcpSocket socket_;
    QString host_;
    uint16_t port_;
    QByteArray buffer_;
    bool recvPending_;
    uint16_t msgId_;
    uint16_t msgLength_;

    // 心跳 & 重连
    QTimer *heartbeatTimer_;
    QTimer *reconnectTimer_;
    int reconnectCount_;
    bool autoReconnect_;
};

#endif // TCPMANAGER_H
