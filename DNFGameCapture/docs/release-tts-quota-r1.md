# 语音生成额度 r1（服务端升级）

只替换服务端程序（`server/dist`、`package.json`、`package-lock.json`，依赖在服务器上用 `npm ci` 安装）。
不导入音频、不修改环境配置、不调用付费语音 API。新数据表在服务启动时自动创建。

## 部署

1. 把 `DNF-5.5.4-TTS-Quota-r1.zip` 和 `deploy-dnf-tts-quota-r1.sh` 上传到服务器 `/root/`。
2. root 执行：`bash /root/deploy-dnf-tts-quota-r1.sh`，看到提示后输入 `UPGRADE`。
3. 脚本会校验升级包、准备依赖、停服、完整备份（程序 + 数据库 + 环境配置）、替换程序、启动并自检；失败自动回退到备份。

## 新功能

- 客户端接口（需要 v2 授权会话）：`GET /api/v2/tts/status`、`POST /api/v2/tts/generate`。
- 管理后台首页新增「语音生成额度」：`http://服务器:18881/admin/tts`
  - 全局：总开关、每日总预算（最多 5 元）、每个卡密默认每日 / 每月字数。
  - 单个卡密 / 主播：加减额外额度（日 / 月额度用完后再扣，不过期）、单独设置日 / 月额度、禁止生成、清零今日用量、操作记录。
  - 今日预估花费、全局缓存、最近 14 天统计。

## 成本控制

- 同一音色 + 同一句话全局只合成一次，缓存命中不扣额度。
- 每日总花费按字数 × 单价预估，封顶 5 元（环境变量和后台都只能调低）；额外额度也不能突破。
- 单句 ≤ 30 字、字符白名单、敏感词过滤（可用 `TTS_BLOCKLIST_FILE` 追加）、每卡密每分钟最多 4 次真正合成。

## 可选环境变量（不设置就用默认值）

`TTS_GENERATE_ENABLED`、`TTS_DAILY_BUDGET_YUAN`（≤5）、`TTS_LICENSE_DAILY_CHARS`（1500）、`TTS_LICENSE_MONTHLY_CHARS`（20000）、
`TTS_MAX_TEXT_CHARS`（30）、`TTS_PRICE_SEED2_PER_10K`（3）、`TTS_PRICE_SEED1_PER_10K`（5）、`TTS_CACHE_MAX_MB`（1024）、`TTS_BLOCKLIST_FILE`。
