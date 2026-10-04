const nodemailer = require('nodemailer');
const configModule = require('./config');

/* 1. 纯单连接，无池化，无脏数据 */
function createTransport() {
  return nodemailer.createTransport({
    host: 'smtp.qq.com',
    port: 465,
    secure: true,
    // ****** 池化全部去掉 ******
    connectionTimeout: 15 * 1000,   // 首次冷启动 15s
    greetingTimeout: 10 * 1000,
    socketTimeout: 30 * 1000,
    auth: {
      user: configModule.email_user,
      pass: configModule.email_pass
    }
  });
}

let transport = createTransport();   // 初始实例

/* 2. 发信 + 超时自动重建（ETIMEDOUT 才重建） */
async function SendMail(mailOptions, retries = 5, delay = 500) {
  try {
    const info = await transport.sendMail(mailOptions);
    console.log('[SMTP] success id=', info.messageId);
    return info;
  } catch (err) {
    console.error(`[SMTP] fail ${err.code} ${err.message}`);
    // 只对网络超时重建 + 重试
    if (retries > 0 && err.code === 'ETIMEDOUT') {
      console.log(`[SMTP] rebuild transport after ${delay}ms, left ${retries - 1}`);
      transport.close();                                    // 关闭坏连接
      transport = createTransport();                        // 新建
      await new Promise(r => setTimeout(r, delay));         // 指数退避
      return SendMail(mailOptions, retries - 1, delay * 2); // 递归
    }
    // 其他错误（包括 EENVELOPE）直接抛，不再重建
    throw err;
  }
}

/* 3. 优雅退出 */
module.exports.SendMail = SendMail;
module.exports.close = () => transport.close();