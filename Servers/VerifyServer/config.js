const fs = require('fs')

// config.json 为本机私有配置（已被 .gitignore 忽略，不入库），可直接写明文，
// 仓库模板见 config.json.example，首次部署 cp config.json.example config.json。
let config = JSON.parse(fs.readFileSync('./config.json', 'utf8'))

let email_user = config.email.user
let email_pass = config.email.pass
let redis_host = config.redis.host
let redis_port = config.redis.port

module.exports = { email_user, email_pass, redis_host, redis_port }
