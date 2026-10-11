const { default: Redis } = require('ioredis')
const configModule = require('./config')

const redisCli = new Redis({
    host: configModule.redis_host,
    port: configModule.redis_port
})

redisCli.on('error', function (err) {
    console.log('redisCli connect error is ', err)
    redisCli.quit()
})

async function GetRedis(key) {
    try {
        const ret = await redisCli.get(key)
        return ret;
    } catch (error) {
        console.log('getRedis error is ', error)
    }
}

// 写入验证码并设置过期时间；SET 与 EXPIRE 任一失败都返回 false
async function SetRedisExpire(key, value, exptime) {
    try {
        await redisCli.set(key, value)
        await redisCli.expire(key, exptime)
        return true
    } catch (error) {
        console.log('setRedisExpire error is ', error)
        return false
    }
}

module.exports = { GetRedis, SetRedisExpire }
