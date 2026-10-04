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
        if (ret === null) {
            console.log(`result<${ret}>, this key cannot be find...`)
        } else {
            console.log(`result<${ret}>, get key success!...`)
        }
        return ret;
    } catch (error) {
        console.log('getRedis error is ', error)
    }
}

async function QueryRedis(key) {
    try {
        const ret = await redisCli.exists(key)
        if (ret === null) {
            console.log(`result<${ret}>, this key is null`)
        } else {
            console.log(`result<${ret}>, with this value...`)
        }
        return ret;
    } catch (error) {
        console.log('queryRedis error is ', error)
    }
}

async function SetRedisExpire(key, value, exptime) {
    try {
        await redisCli.set(key, value)
        await redisCli.expire(key, exptime)
        return true
        return ret;
    } catch (error) {
        console.log('setRedisExpire error is ', error)
        return false
    }
}

function Quit() {
    redisCli.quit()
}

module.exports = { GetRedis, QueryRedis, Quit, SetRedisExpire }