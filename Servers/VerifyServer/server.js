const grpc = require('@grpc/grpc-js')
const messageProto = require('./proto')
const constantsModule = require('./constants')
const emailModule = require('./email')
const configModule = require('./config');
const redisModule = require('./redis')
const verifyCodeModule = require('./verify-code')

async function GracefulShutdown() {
    console.log('\n[EXIT] closing email pool...')
    await emailModule.close()        // ③ 真正关闭连接池
    console.log('[EXIT] pool closed, bye.')
    process.exit(0)
}

// 放在所有服务启动之后
process.on('SIGINT', GracefulShutdown)   // Ctrl+C
process.on('SIGTERM', GracefulShutdown)  // kill $PID

async function GetVerifyCode(call, callback) {
    console.log('email is ', call.request.email)
    try {
        const queryRes = await redisModule.GetRedis(constantsModule.code_prefix + call.request.email);
        let uniqueId = queryRes;
        if (queryRes === null) {
            uniqueId = verifyCodeModule.GenerateVerifyCode();
            const bres = await redisModule.SetRedisExpire(constantsModule.code_prefix + call.request.email, uniqueId,
                verifyCodeModule.CODE_TTL_SECONDS)
            if (!bres) {
                callback(null, { email: call.request.email, error: constantsModule.Errors.VERIFY_REDIS_ERROR })
                return;
            }
            const expireAt = Date.now() + verifyCodeModule.CODE_EXPIRE_MS
            const timeStr = verifyCodeModule.FormatExpireTime(expireAt)

            const text_str = verifyCodeModule.BuildMailText(uniqueId, timeStr)

            const mailOptions = {
                from: configModule.email_user,
                to: call.request.email,
                subject: '验证码',
                text: text_str
            }

            const res = await emailModule.SendMail(mailOptions)
            console.log('[SMTP] sent, id=', res.messageId)
            callback(null, { email: call.request.email, error: constantsModule.Errors.SUCCESS })
        } else {
            callback(null, { email: call.request.email, error: constantsModule.Errors.VERIFY_CODE_EXISTS })
        }
    } catch (err) {
        console.error(
            `[SMTP] ${new Date().toISOString()} ${err.code} ${err.command} ${err.message}`
        );
        callback(null, { email: call.request.email, error: constantsModule.Errors.VERIFY_MAIL_FAILED })
    }
}

function main() {
    const server = new grpc.Server();
    server.addService(messageProto.VerifyService.service, { GetVerifyCode: GetVerifyCode })
    server.bindAsync('127.0.0.1:50051', grpc.ServerCredentials.createInsecure(), () => {
        console.log('grpc server started...')
    })
}

main()
