#include "msg_node.h"

namespace P1 {
MsgNode::MsgNode(uint16_t maxLen, uint16_t msgId) : totalLen_(maxLen), curLen_(0), msgId_(msgId), data_(maxLen) {}

void MsgNode::Clear()
{
    curLen_ = 0;
}

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

uint16_t MsgNode::GetTotalLen() const
{
    return totalLen_;
}

uint16_t MsgNode::GetMsgId() const
{
    return msgId_;
}

void MsgNode::SetCurLen(uint16_t len)
{
    if (len > totalLen_) {
        len = totalLen_;
    }
    curLen_ = len;
}

void MsgNode::SetMsgId(uint16_t id)
{
    msgId_ = id;
}

void MsgNode::SetTotalLength(uint16_t len)
{
if (len > data_.capacity()) {
        data_.reserve(len);
        data_.resize(len); 
    } else {
        if (data_.size() < len) {
            data_.resize(len);
        }
    }
    totalLen_ = len;
    curLen_ = 0; 
}

uint16_t MsgNode::WritableBytes() const
{
    return totalLen_ - curLen_;
}
} // namespace P1