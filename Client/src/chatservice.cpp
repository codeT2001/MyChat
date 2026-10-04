#include "chatservice.h"
#include "tcpmanager.h"
#include "jsoncodec.h"
#include "usermanager.h"
#include "userdata.h"
#include "log.h"

ChatService::ChatService()
{
    connect(&TcpManager::GetInstance(), &TcpManager::SigMessageReceived, this, &ChatService::OnMessageReceived);
}

void ChatService::SendTextChatMsg(int fromUid, int toUid, const QJsonArray &textArray)
{
    QByteArray data = JsonSerializer::SerializeTextChatMsgReq(fromUid, toUid, textArray);
    TcpManager::GetInstance().SlotSendData(RequestId::TEXT_CHAT_MSG_REQ, data);
}

void ChatService::OnMessageReceived(RequestId id, const QByteArray &data)
{
    switch (id) {
        case RequestId::TEXT_CHAT_MSG_RSP: {
            // 我方发送消息后服务器的送达响应
            ErrorCodes err = JsonParser::ParseTextChatMsgRsp(data);
            if (err != ErrorCodes::SUCCESS) {
                LOG_WARN() << "TextChatMsg rsp error:" << static_cast<int>(err);
                break;
            }
            LOG_DEBUG() << "TextChatMsg rsp success, message delivered";
            // TODO: 根据 msgid 更新气泡的"已送达"状态
            break;
        }
        case RequestId::NOTIFY_TEXT_CHAT_MSG_REQ: {
            // 对端发来的文本消息通知
            auto result = JsonParser::ParseNotifyTextChatMsg(data);
            if (result.error != ErrorCodes::SUCCESS) {
                LOG_WARN() << "NotifyTextChatMsg error:" << static_cast<int>(result.error);
                break;
            }
            LOG_DEBUG() << "NotifyTextChatMsg from uid:" << result.fromUid << "msg count:" << result.msgs.size();
            if (result.msgs.empty()) {
                break;
            }
            // 写入该好友的历史消息
            UserManager::GetInstance().AppendFriendChatMsg(result.fromUid, result.msgs);
            // 更新会话列表预览用的最后一条消息
            auto friendInfo = UserManager::GetInstance().GetFriendById(result.fromUid);
            if (friendInfo) {
                friendInfo->last_msg_ = result.msgs.back()->msg_content_;
            }
            emit SigTextChatMsgReceived(result.fromUid, result.msgs);
            break;
        }
        default:
            break;
    }
}
