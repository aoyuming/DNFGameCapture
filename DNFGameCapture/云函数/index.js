const express = require('express');
const OSS = require('ali-oss');

const app = express();
app.use(express.json());

app.post('/*', async (req, res) => {
    try {
        const data = req.body;
        const licenseKey = data.key;
        const hwid = data.hwid;
        const duration = data.duration || 0; // 【新增】：接收客户端传来的时长（秒）

        if (!licenseKey || !hwid) {
            return res.json({ status: "error", msg: "缺少参数" });
        }

        // ==========================================
        // 【新增】：云端终极绞杀线！彻底封杀老版本卡密
        // ==========================================
        if (!licenseKey.startsWith('CDK-')) {
            return res.json({ status: "error", msg: "此旧版卡密已停用，请下载最新版软件并联系管理员更换CDK！" });
        }
        // ==========================================

        const accessKeyId = process.env.ALIBABA_CLOUD_ACCESS_KEY_ID;
        const accessKeySecret = process.env.ALIBABA_CLOUD_ACCESS_KEY_SECRET;
        const securityToken = process.env.ALIBABA_CLOUD_SECURITY_TOKEN;

        if (!accessKeyId) {
            return res.json({ status: "error", msg: "云端未获取到OSS权限(环境变量为空)" });
        }

        const client = new OSS({
            region: 'oss-cn-beijing',         
            accessKeyId: accessKeyId,
            accessKeySecret: accessKeySecret,
            stsToken: securityToken,          
            bucket: 'dnf-capture-update'
        });

        const fileName = `licenses/${licenseKey}.txt`;

        try {
            // 【老用户/已激活验证逻辑】
            const result = await client.get(fileName);
            const content = result.content.toString();
            
            // 兼容老数据格式(仅hwid) 和 新数据格式(hwid|expireTime)
            const parts = content.split('|');
            const boundHwid = parts[0];
            const expireTime = parts.length > 1 ? parseInt(parts[1], 10) : 0;

            if (boundHwid === hwid) {
                const nowSec = Math.floor(Date.now() / 1000);
                // 如果云端有到期时间，且不是永久卡，且已经超过当前时间
                if (expireTime > 0 && expireTime !== 0xFFFFFFFF && nowSec > expireTime) {
                    res.json({ status: "error", msg: "该卡密已过期，请续费！" });
                } else {
                    res.json({ status: "ok", msg: "验证通过", expireTime: expireTime });
                }
            } else {
                res.json({ status: "error", msg: `该卡密已被设备(${boundHwid.substring(0, 4)}***)绑定，禁止多开！` });
            }
        } catch (e) {
            if (e.name === 'NoSuchKeyError') {
                // 【新用户首次激活逻辑】
                try {
                    let expireTime = 0;
                    if (duration > 0) {
                        if (duration === 0xFFFFFFFF) {
                            expireTime = 0xFFFFFFFF; // 永久卡
                        } else {
                            expireTime = Math.floor(Date.now() / 1000) + duration; // 当前时间 + 时长
                        }
                    }
                    
                    // 老卡密不带duration，仅存hwid；新卡密存 hwid|expireTime
                    const writeContent = expireTime > 0 ? `${hwid}|${expireTime}` : hwid;

                    await client.put(fileName, Buffer.from(writeContent), {
                        headers: { 'x-oss-forbid-overwrite': 'true' }
                    });
                    
                    res.json({ status: "ok", msg: "首次激活，绑定成功！", expireTime: expireTime });
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