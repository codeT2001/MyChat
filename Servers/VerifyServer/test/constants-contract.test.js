// 跨语言错误码契约：VerifyServer(constants.js) 必须与 C++ constants.h 保持一致
// （constants.js 顶部注释里写明了这条约束，这里把它变成可执行的测试）
const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const { Errors, code_prefix } = require('../constants')

const CPP_HEADER = path.join(__dirname, '..', '..', 'common', 'include', 'chat', 'constants.h')
const cppSource = fs.readFileSync(CPP_HEADER, 'utf8')

// 从 C++ 枚举里取某个符号的值
function cppValue(name) {
    const m = cppSource.match(new RegExp(`${name}\\s*=\\s*(\\d+)`))
    assert.ok(m, `C++ constants.h 里找不到 ${name}`)
    return Number(m[1])
}

test('VerifyServer 错误码与 C++ ErrorCodes 同值', () => {
    assert.strictEqual(Errors.SUCCESS, cppValue('SUCCESS'))
    assert.strictEqual(Errors.VERIFY_CODE_EXISTS, cppValue('VERIFY_CODE_EXISTS'))
    assert.strictEqual(Errors.VERIFY_REDIS_ERROR, cppValue('VERIFY_REDIS_ERROR'))
    assert.strictEqual(Errors.VERIFY_MAIL_FAILED, cppValue('VERIFY_MAIL_FAILED'))
})

test('VerifyServer 专属错误码落在预留段 20001+，不与通用码撞值', () => {
    const specific = ['VERIFY_CODE_EXISTS', 'VERIFY_REDIS_ERROR', 'VERIFY_MAIL_FAILED']
    for (const name of specific) {
        assert.ok(Errors[name] >= 20001, `${name}=${Errors[name]} 未落在预留段`)
    }
    assert.strictEqual(new Set(specific.map(n => Errors[n])).size, specific.length, '错误码重复')
})

test('验证码 Redis key 前缀一致', () => {
    assert.strictEqual(code_prefix, 'code_')
})
