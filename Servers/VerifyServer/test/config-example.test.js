// 部署模板自检：config.json.example 必须能解析且字段齐全
// （config.json 为私有配置已被 .gitignore 忽略，新部署需 cp 模板，模板坏掉会导致起不来）
const test = require('node:test')
const assert = require('node:assert')
const fs = require('node:fs')
const path = require('node:path')

const examplePath = path.join(__dirname, '..', 'config.json.example')

test('config.json.example 是合法 JSON 且字段齐全', () => {
    const cfg = JSON.parse(fs.readFileSync(examplePath, 'utf8'))
    assert.strictEqual(typeof cfg.email.user, 'string')
    assert.strictEqual(typeof cfg.email.pass, 'string')
    assert.strictEqual(typeof cfg.redis.host, 'string')
    assert.strictEqual(typeof cfg.redis.port, 'string')
    assert.ok(cfg.email.user.length > 0)
})
