#include "msg_node.h"

namespace P1 {
MsgNode::MsgNode(uint16_t maxLen, uint16_t msgId) : curLen_(0), totalLen_(maxLen), msgId_(msgId), data_(maxLen) {}

char* MsgNode::GetData()
{
    return data_.data();
}

const char* MsgNode::GetData() const
{
    return data_.data();
}

uint16_t MsgNode::GetCurLen() const
{
    return curLen_;
}

uint16_t MsgNode::GetMsgId() const
{
    return msgId_;
}

void MsgNode::SetCurLen(uint16_t len)
{
    curLen_ = (len > totalLen_) ? totalLen_ : len;
}
} // namespace P1
