const express = require('express');
const { v4: uuidv4 } = require('uuid');
const { SendMail } = require('./email');
const configModule = require('./config');

const app = express();
app.use(express.json());

/** POST /sendCode
 * body: { "email": "3308849335@qq.com" }
 */
app.post('/sendCode', async (req, res) => {
  const { email } = req.body;
  if (!email) return res.status(400).json({ error: 'missing email' });

  try {
    const code = uuidv4();                       // 生成验证码
    await SendMail({
      from: configModule.email_user,
      to: email,
      subject: '验证码',
      text: `您的验证码为 ${code}，请在 3 分钟内完成注册。`
    });
    return res.json({ email, error: 0 });        // 成功
  } catch (err) {
    console.error('[HTTP] send fail', err);
    return res.status(500).json({ error: 1, message: err.message });
  }
});

const PORT = 3000;
app.listen(PORT, () => console.log(`HTTP server ready on http://localhost:${PORT}/sendCode`));