#ifndef _P1_MSG_NODE_H_
#define _P1_MSG_NODE_H_

#include <cstdint>
#include <vector>

namespace P1 {

// 收/发报文的裸缓冲：只承担长度边界校验，不做协议解析
class MsgNode {
public:
    explicit MsgNode(uint32_t maxLen, uint16_t msgId = 0);

    ~MsgNode() = default;

    MsgNode(const MsgNode&) = delete;
    MsgNode& operator=(const MsgNode&) = delete;
    MsgNode(MsgNode&&) = default;
    MsgNode& operator=(MsgNode&&) = default;

    char* GetData();
    const char* GetData() const;

    uint32_t GetCurLen() const;
    uint16_t GetMsgId() const;

    // 写入当前已用长度；超过 totalLen_ 时按 totalLen_ 截断
    void SetCurLen(uint32_t len);

private:
    uint32_t curLen_;
    uint32_t totalLen_;
    uint16_t msgId_;
    std::vector<char> data_;
};
} // namespace P1

#endif // _P1_MSG_NODE_H_
