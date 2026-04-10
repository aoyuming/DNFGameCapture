const OSS = require('ali-oss');
const readline = require('readline');




// ==========================================
// 1. 配置你的阿里云 OSS 凭证
// ==========================================
const client = new OSS({
    region: 'oss-cn-beijing',            // 你的可用区
    accessKeyId: '',     // 替换为你的 AK
    accessKeySecret: ', // 替换为你的 SK
    // stsToken: '如果有STS_TOKEN就填，用主账号AKSK就删掉这一行', 
    bucket: 'dnf-capture-update'         // 你的 Bucket 名称
});

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

// ==========================================
// 工具函数：时间戳转日期
// ==========================================
function formatTime(sec) {
    if (sec === 0) return "未记录(旧版数据)";
    if (sec === 1) return "已封停";
    if (sec === 0xFFFFFFFF) return "永久有效";
    const d = new Date(sec * 1000);
    return d.toLocaleString('zh-CN', { hour12: false });
}

// ==========================================
// 核心逻辑
// ==========================================
async function getLicense(key) {
    try {
        const result = await client.get(`licenses/${key}.txt`);
        const content = result.content.toString();
        const parts = content.split('|');
        return {
            exists: true,
            hwid: parts[0],
            expireTime: parts.length > 1 ? parseInt(parts[1], 10) : 0
        };
    } catch (e) {
        if (e.name === 'NoSuchKeyError') return { exists: false };
        throw e;
    }
}

async function saveLicense(key, hwid, expireTime) {
    const writeContent = `${hwid}|${expireTime}`;
    await client.put(`licenses/${key}.txt`, Buffer.from(writeContent));
}

// ==========================================
// 控制台交互菜单
// ==========================================
const question = (query) => new Promise(resolve => rl.question(query, resolve));

async function mainMenu() {
    console.log('\n==================================');
    console.log('    🛡️ DNF 授权超级管理控制台 🛡️');
    console.log('==================================');
    console.log(' 1. 查询卡密状态');
    console.log(' 2. 封停卡密 (拉黑)');
    console.log(' 3. 增加/减少时长 (补偿/续费)');
    console.log(' 4. 解除设备绑定 (换绑电脑)');
    console.log(' 0. 退出程序');
    console.log('==================================');

    const choice = await question('请输入操作序号 (0-4): ');

    if (choice === '0') {
        console.log('👋 退出管理系统...');
        rl.close();
        return;
    }

    const key = await question('🔑 请输入目标卡密 (如 CDK-XXX...): ');
    if (!key.startsWith('CDK-') && !key.startsWith('DNF-')) {
        console.log('❌ 卡密格式错误！');
        return mainMenu();
    }

    console.log('⏳ 正在查询云端数据...');
    const data = await getLicense(key);

    if (!data.exists) {
        console.log('❌ 找不到该卡密！可能是新卡密还未被用户激活过。');
        return mainMenu();
    }

    console.log('\n✅ 当前卡密信息：');
    console.log(` - 绑定机器码: ${data.hwid}`);
    console.log(` - 到期时间: ${formatTime(data.expireTime)} (时间戳: ${data.expireTime})`);

    try {
        switch (choice) {
            case '1':
                // 仅查询，啥也不干
                break;

            case '2': // 封号
                const confirmBlock = await question('⚠️ 确定要封停该卡密吗？(y/n): ');
                if (confirmBlock.toLowerCase() === 'y') {
                    await saveLicense(key, data.hwid, 1);
                    console.log('💀 封停成功！该用户下次打开软件将被立刻拦截！');
                }
                break;

            case '3': // 加时长
                const daysStr = await question('⏱️ 请输入要增加的天数 (支持负数扣除): ');
                const days = parseFloat(daysStr);
                if (!isNaN(days)) {
                    if (data.expireTime === 1) {
                        console.log('⚠️ 该卡密已被封停，请先解除封停状态（直接输入新的到期时间戳或用换绑功能重置）。');
                    } else if (data.expireTime === 0xFFFFFFFF) {
                        console.log('🌟 该卡密已经是永久卡，无需加时间！');
                    } else {
                        // 如果是老卡密时间是 0，从现在开始加
                        const baseTime = data.expireTime > 0 ? data.expireTime : Math.floor(Date.now() / 1000);
                        const newExpireTime = baseTime + Math.floor(days * 24 * 60 * 60);
                        await saveLicense(key, data.hwid, newExpireTime);
                        console.log(`🎉 操作成功！最新到期时间更新为: ${formatTime(newExpireTime)}`);
                    }
                }
                break;

            case '4': // 换绑
                const newHwid = await question('💻 请输入新的机器码 (输入 RESET 将重置为首登绑定): ');
                let finalHwid = newHwid;
                if (newHwid.toUpperCase() === 'RESET') {
                    finalHwid = 'UNBOUND_WAITING_FOR_LOGIN';
                }
                await saveLicense(key, finalHwid, data.expireTime);
                console.log(`🔄 换绑成功！目标机器码已变更为: ${finalHwid}`);
                break;

            default:
                console.log('❌ 无效的序号！');
        }
    } catch (err) {
        console.log('❌ 操作过程中发生云端异常: ', err.message);
    }

    setTimeout(mainMenu, 1000); // 延迟1秒回到主菜单
}

// 启动程序
mainMenu();