const grpc = require('@grpc/grpc-js')
const messageProto = require('./proto')
const constantsMdule = require('./constants')
const { v4: uuidv4 } = require('uuid')
const emailMdoule = require('./email')
const configModule = require('./config');
const redisModule = require('./redis')

async function GracefulShutdown() {
    console.log('\n[EXIT] closing email pool...')
    await emailMdoule.close()        // ③ 真正关闭连接池
    console.log('[EXIT] pool closed, bye.')
    process.exit(0)
}

// 放在所有服务启动之后
process.on('SIGINT', GracefulShutdown)   // Ctrl+C
process.on('SIGTERM', GracefulShutdown)  // kill $PID

async function GetVerifyCode(call, callback) {
    console.log('email is ', call.request.email)
    try {
        let queryRes = await redisModule.GetRedis(constantsMdule.code_prefix + call.request.email);
        console.log('queryResult is ', queryRes)
        let uniqueId = queryRes;
        if (queryRes === null) {
            uniqueId = uuidv4();
            if (uniqueId.length > 4) {
                uniqueId = uniqueId.substring(0, 4);
            }
            let bres = await redisModule.SetRedisExpire(constantsMdule.code_prefix + call.request.email, uniqueId, 600)
            if (!bres) {
                callback(null, { email: call.request.email, error: constantsMdule.Errors.REDIS_ERROR })
                return;
            }
            const expireAt = new Date(Date.now() + 10 * 60 * 1000)
            const timeStr = expireAt.toLocaleString('zh-CN', {
                hour: '2-digit',
                minute: '2-digit',
                hour12: false,
                timeZone: 'Asia/Shanghai'
            })

            const text_str = `您的验证码是 ${uniqueId}，请在 ${timeStr} 前完成注册。`

            let mailOptions = {
                from: configModule.email_user,
                to: call.request.email,
                subject: '验证码',
                text: text_str
            }

            let res = await emailMdoule.SendMail(mailOptions)
            console.log('send res is ', res)
            callback(null, { email: call.request.email, error: constantsMdule.Errors.SUCCESS })
        } else {
            callback(null, { email: call.request.email, error: constantsMdule.Errors.VERIFICATION_CODE_EXISTS })
        }
        console.log('uniqueId is ', uniqueId)
    } catch (err) {
        console.error(
            `[SMTP] ${new Date().toISOString()} ${err.code} ${err.command} ${err.message}`
        );
        callback(null, { email: call.request.email, error: constantsMdule.Errors.EXCEPTION })
    }
}

function main() {
    let server = new grpc.Server();
    server.addService(messageProto.VerifyService.service, { GetVerifyCode: GetVerifyCode })
    server.bindAsync('127.0.0.1:50051', grpc.ServerCredentials.createInsecure(), () => {
        console.log('grpc server started...')
    })
}

main()