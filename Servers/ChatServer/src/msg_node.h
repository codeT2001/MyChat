#ifndef _P1_PACKET_H_
#define _P1_PACKET_H_

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
namespace P1 {

class MsgNode {
public:
    explicit MsgNode(uint16_t maxLen, uint16_t msgId = -1);

    ~MsgNode() = default;

    MsgNode(const MsgNode&) = delete;
    MsgNode& operator=(const MsgNode&) = delete;
    MsgNode(MsgNode&&) = default;
    MsgNode& operator=(MsgNode&&) = default;
    void Clear();

    char* GetData();

    const char* GetData() const;

    uint16_t GetCurLen() const;

    uint16_t GetTotalLen() const;

    uint16_t GetMsgId() const;

    void SetCurLen(uint16_t len);

    void SetMsgId(uint16_t id);

    void SetTotalLength(uint16_t len);

    uint16_t WritableBytes() const;

private:
    uint16_t curLen_;
    uint16_t totalLen_;
    uint16_t msgId_;
    std::vector<char> data_;
};
} // namespace P1
#endif