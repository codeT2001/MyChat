let code_prefix = "code_";

// 错误码：与 C++ ErrorCodes 使用同一套编号（common/include/chat/constants.h）。
// 0 复用通用 SUCCESS；本服务特有码收敛到预留区间 20001+，避免与通用码（0~10）撞值。
// 修改本表时必须同步：Servers/common/include/chat/constants.h 与 Client/inc/constants.h。
const Errors = {
    SUCCESS: 0,                 // 验证码已生成并发送
    VERIFY_CODE_EXISTS: 20001,  // 该邮箱 10 分钟内已有有效验证码，不重发
    VERIFY_REDIS_ERROR: 20002,  // 验证码写入 Redis 失败
    VERIFY_MAIL_FAILED: 20003,  // 验证码邮件发送失败
}

module.exports = { code_prefix, Errors }
