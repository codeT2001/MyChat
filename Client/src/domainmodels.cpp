#include "domainmodels.h"

void FriendInfo::AppendChatMsgs(const std::vector<std::shared_ptr<TextChatData>> &text_vec)
{
    for (const auto &text : text_vec) {
        chat_msgs_.push_back(text);
    }
}
