# DNF 云匹配服务器 5.2.3 部署说明

本发布包只升级服务器到 **5.2.3**，本地客户端保持 **5.2.2**，不需要重新分发客户端。必须先部署服务器并完成健康检查，再恢复主播投稿和同步流量。

发布版本关系：服务器 5.2.3，客户端保持 5.2.2。

## 本版功能

- 记录授权请求与主播 Socket 的服务器端 IP 观察。
- 只在同一公网 IP、120 秒内、授权身份和在线主播均唯一时自动关联；共享 IP 或候选不唯一时不猜测。
- 主播管理显示当前 IP、最后 IP、离线 `geoip-lite` 地区和对应密钥，可人工绑定或换绑；人工结果优先。
- 选中主播后，通过既有单条 `/reveal` 接口直接显示完整卡密。列表和状态接口不会批量返回卡密明文。
- 密钥管理显示激活主播；共享选手库的待审投稿显示来源主播并支持按主播搜索。
- 安装、健康检查和打包不会自动执行任何真实冲突合并，合并仍由管理员确认。

## 生产约定

- 服务器包：`dnf-cloud-match-server-production-5.2.3.zip`
- 客户端版本：`5.2.2`
- 公网地址：`http://47.109.149.111:18880`
- 管理端：服务器本机 `127.0.0.1:18881`
- systemd 服务：`dnf-cloud-match.service`
- 程序目录：`/opt/dnf-cloud-match-server`
- 数据目录：`/var/lib/dnf-cloud-match`
- 环境文件：`/etc/default/dnf-cloud-match`
- 默认数据库：`/var/lib/dnf-cloud-match/cloud-match.sqlite`
- 发布清单：`https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json`

数据库旁的 `.license-key` 是卡密保险库主密钥，必须和 SQLite、WAL/SHM 文件一起备份。不得用测试数据库覆盖生产数据库，也不得把管理员密码、数据库或 `.license-key` 放进发布 ZIP。

## 文件放在 /root 时的正确命令

下面假定 ZIP 和 `.sha256.txt` 都已经上传到 `/root`。之前出现 `deploy/install.sh: No such file or directory`，是因为命令在 `/root` 执行，而脚本实际位于 ZIP 解压后的 `dnf-cloud-match-server/deploy` 目录。

```bash
cd /root
sha256sum -c dnf-cloud-match-server-production-5.2.3.sha256.txt

release_dir="/root/dnf-cloud-match-5.2.3-$(date +%Y%m%d-%H%M%S)"
install -d -m 700 "$release_dir"
unzip -q dnf-cloud-match-server-production-5.2.3.zip -d "$release_dir"
cd "$release_dir/dnf-cloud-match-server"

test -f deploy/install.sh
test -f deploy/preflight.sh
sha256sum -c SHA256SUMS.txt

sudo bash deploy/install.sh
sudo bash deploy/preflight.sh http://127.0.0.1:18880 /etc/default/dnf-cloud-match
bash deploy/preflight.sh http://47.109.149.111:18880
```

如果服务器缺少 `unzip`、Node.js 20+、npm、curl、CA 证书、Python 3 或原生模块编译工具，请在维护窗口前安装。安装脚本使用 `npm ci --omit=dev`，会按锁文件安装 `geoip-lite` 等运行时依赖，因此部署时需要能够访问 npm registry。

## 部署前

1. 在授权维护窗口停止入口流量和其他数据库写入者。
2. 备份 `/opt/dnf-cloud-match-server`、完整 `/var/lib/dnf-cloud-match`、`/etc/default/dnf-cloud-match` 和 systemd 单元。
3. 确认备份包含 SQLite、WAL/SHM、`.license-key`，并保留安装器生成的 `backup.complete` 标记。
4. 检查现有环境文件中的 `DATABASE_PATH`。非标准数据库路径、符号链接数据目录或自定义 systemd drop-in 需要单独核对。
5. 确认磁盘空间足够同时容纳旧程序、完整数据备份、新程序和新 `node_modules`。

安装器会串行化升级，先停止并确认旧服务已停止，再备份、安装依赖和替换程序。已有选手库、投稿、卡密、授权会话、主播和人工关联都保存在原生产数据库中。失败时会尝试恢复同一套程序、数据、环境和服务状态；备份默认位于 `/var/backups/dnf-cloud-match/upgrade-*`。

## 健康检查

部署命令完成后确认：

```bash
systemctl is-active dnf-cloud-match.service
curl -fsS http://127.0.0.1:18880/health
curl -fsS http://127.0.0.1:18880/api/v2/health
curl -sS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:18880/api/v2/player-library
curl -fsS http://127.0.0.1:18881/admin/health
```

预期 `/health` 和 `/api/v2/health` 返回 200，未授权 `/api/v2/player-library` 返回 **401**。返回 **404** 表示仍是旧服务器或路由错误。`/api/v2/health` 应报告 `protocolVersion:2`、正确公网地址和实际 `ALLOW_LEGACY_PERMANENT_KEYS` 布尔值。

不要用真实用户卡密 POST `/auth/activate` 做探针，这会产生或改变绑定。`ALLOW_LEGACY_PERMANENT_KEYS=false` 必须保持原样，安装器不会偷偷打开显式关闭的兼容策略。

## 管理员验证

管理端默认只监听回环地址。可从管理电脑建立 SSH 隧道：

```bash
ssh -L 18881:127.0.0.1:18881 root@47.109.149.111
```

然后访问 `http://127.0.0.1:18881/admin`：

1. 在主播管理选择一个主播，确认当前/最后 IP、地区和对应完整卡密正常。
2. 对无法自动识别的共享 IP 场景，选择正确已激活密钥并执行人工绑定。
3. 在密钥管理确认绑定设备行显示主播名。
4. 在共享库确认待审核投稿显示来源主播，现有冲突可继续批量确认或按名称一键合并。
5. 确认已有选手、卡密、主播和投稿数量没有异常后，再恢复入口流量。

管理端使用 Basic Auth，生产公网目前仍是 HTTP。不要把 18881 直接暴露到公网；应使用 SSH 隧道或可信 TLS 反向代理。

## 回滚

如果安装或检查失败，继续阻断写入并保持服务停止。只选择带 `backup.complete` 的同一次备份，整体恢复其中的程序、完整数据目录、环境文件和 systemd 单元，包括 `.license-key`、WAL/SHM；不要只恢复一个 SQLite 文件，也不要重新生成保险库密钥来绕过错误。恢复所有者和权限后执行 daemon-reload，并按备份记录恢复原来的 enable/active 状态。

若自动回滚输出 `MANUAL RECOVERY REQUIRED`，不要启动旧代码连接可能已迁移的数据，先保存失败现场并按上述完整备份集人工恢复。

## 本地打包验证

在仓库根目录执行：

```powershell
./scripts/package-production-5.2.3.ps1 -ValidateOnly
./scripts/package-production-5.2.3.ps1
```

打包器在临时目录执行干净 TypeScript 编译，只允许 `dist/*.js`、部署脚本、锁文件和指定说明进入 ZIP，并生成包内 `SHA256SUMS.txt` 和 ZIP 的 SHA-256 旁车文件。它不会连接或修改生产服务器，也拒绝覆盖已有 5.2.3 产物。
