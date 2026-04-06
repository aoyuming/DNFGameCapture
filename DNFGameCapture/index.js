const express = require('express');
const OSS = require('ali-oss');

const app = express();
app.use(express.json());

app.post('/*', async (req, res) => {
    try {
        const data = req.body;
        const licenseKey = data.key;
        const hwid = data.hwid;

        if (!licenseKey || !hwid) {
            return res.json({ status: "error", msg: "缺少参数" });
        }

        const accessKeyId = process.env.ALIBABA_CLOUD_ACCESS_KEY_ID;
        const accessKeySecret = process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET;
        const securityToken = process.env.ALIBABA_CLOUD_SECURITY_TOKEN;

        if (!accessKeyId) {
            return res.json({ status: "error", msg: "云端未获取到OSS权限(环境变量为空)" });
        }

        // 初始化 OSS 客户端
        const client = new OSS({
            // 👇 注意：这里要填你刚才自己确认好的真实地域，比如 oss-cn-beijing 或 oss-cn-shanghai
            region: 'oss-cn-beijing',         
            accessKeyId: accessKeyId,
            accessKeySecret: accessKeySecret,
            // 👇 【大坑修复】：官方要求这里必须叫 stsToken，不能叫 securityToken！
            stsToken: securityToken,          
            bucket: 'dnf-capture-update'
        });

        const fileName = `licenses/${licenseKey}.txt`;

        try {
            const result = await client.get(fileName);
            const boundHwid = result.content.toString();

            if (boundHwid === hwid) {
                res.json({ status: "ok", msg: "验证通过" });
            } else {
                res.json({ status: "error", msg: `该卡密已被设备(${boundHwid.substring(0, 4)}***)绑定，禁止多开！` });
            }
        } catch (e) {
            if (e.name === 'NoSuchKeyError') {
                try {
                    await client.put(fileName, Buffer.from(hwid), {
                        headers: { 'x-oss-forbid-overwrite': 'true' }
                    });
                    res.json({ status: "ok", msg: "首次激活，绑定成功！" });
                } catch (putErr) {
                    res.json({ status: "error", msg: "激活冲突，请重试！" });
                }
            } else {
                res.json({ status: "error", msg: "OSS请求异常: " + e.message });
            }
        }
    } catch (err) {
        res.json({ status: "error", msg: "服务器内部错误" });
    }
});

app.listen(9000, '0.0.0.0', () => {
    console.log('Server is running on port 9000');
});