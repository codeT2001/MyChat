// 验证码相关的纯逻辑：从 server.js 抽出，便于离线单测（不依赖 Redis / SMTP / gRPC）
const { v4: uuidv4 } = require('uuid')

// 验证码有效期：Redis key 过期时间（秒）与邮件里提示的截止时间（毫秒）需保持一致
const CODE_TTL_SECONDS = 600
const CODE_EXPIRE_MS = 10 * 60 * 1000
const CODE_LENGTH = 4

// 生成验证码：取 uuid 的前 4 位（同一邮箱再次请求时直接复用 Redis 里的旧码）
function GenerateVerifyCode() {
    const uniqueId = uuidv4()
    return uniqueId.length > CODE_LENGTH ? uniqueId.substring(0, CODE_LENGTH) : uniqueId
}

// 邮件里的截止时间：固定按 Asia/Shanghai 显示为 HH:MM
function FormatExpireTime(expireAtMs) {
    return new Date(expireAtMs).toLocaleString('zh-CN', {
        hour: '2-digit',
        minute: '2-digit',
        hour12: false,
        timeZone: 'Asia/Shanghai'
    })
}

function BuildMailText(code, timeStr) {
    return `您的验证码是 ${code}，请在 ${timeStr} 前完成注册。`
}

module.exports = { CODE_TTL_SECONDS, CODE_EXPIRE_MS, CODE_LENGTH, GenerateVerifyCode, FormatExpireTime, BuildMailText }
