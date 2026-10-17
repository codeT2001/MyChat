// 验证码纯逻辑单测：node --test（Node 内置测试运行器，无需额外依赖、不联网）
const test = require('node:test')
const assert = require('node:assert')

const verifyCode = require('../verify-code')

test('验证码为 4 位十六进制，且多次生成不重复', () => {
    const codes = new Set()
    for (let i = 0; i < 200; ++i) {
        const code = verifyCode.GenerateVerifyCode()
        assert.strictEqual(code.length, verifyCode.CODE_LENGTH)
        assert.match(code, /^[0-9a-f]{4}$/)
        codes.add(code)
    }
    assert.ok(codes.size > 150, `随机性不足，200 次仅产生 ${codes.size} 个不同验证码`)
})

test('验证码有效期：Redis TTL 与邮件截止时间一致', () => {
    assert.strictEqual(verifyCode.CODE_TTL_SECONDS, 600)
    assert.strictEqual(verifyCode.CODE_EXPIRE_MS, 600 * 1000)
    assert.strictEqual(verifyCode.CODE_EXPIRE_MS / 1000, verifyCode.CODE_TTL_SECONDS)
})

test('截止时间按 Asia/Shanghai 显示为 HH:MM', () => {
    // epoch 0 → 东八区 08:00
    assert.strictEqual(verifyCode.FormatExpireTime(0), '08:00')
    // UTC 23:30 → 东八区次日 07:30（跨日不能算错）
    assert.strictEqual(verifyCode.FormatExpireTime(Date.UTC(1970, 0, 1, 23, 30)), '07:30')
    assert.match(verifyCode.FormatExpireTime(Date.now()), /^\d{2}:\d{2}$/)
})

test('邮件文案包含验证码与截止时间', () => {
    const text = verifyCode.BuildMailText('a1b2', '08:05')
    assert.match(text, /a1b2/)
    assert.match(text, /08:05/)
    assert.match(text, /您的验证码是/)
})
