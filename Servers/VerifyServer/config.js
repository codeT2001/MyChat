const fs = require('fs')
const path = require('path')

// config.json 为本机私有配置（已被 .gitignore 忽略，不入库），可直接写明文，
// 仓库模板见 config.json.example，首次部署 cp config.json.example config.json。
// 用 __dirname 定位，避免启动目录不同导致读不到配置
const configPath = path.join(__dirname, 'config.json')
const config = JSON.parse(fs.readFileSync(configPath, 'utf8'))

const email_user = config.email.user
const email_pass = config.email.pass
const redis_host = config.redis.host
const redis_port = config.redis.port

module.exports = { email_user, email_pass, redis_host, redis_port }
