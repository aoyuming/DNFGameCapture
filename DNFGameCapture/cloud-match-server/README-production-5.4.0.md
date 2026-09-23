# DNF 云匹配服务器 5.4.0 部署说明

本次同时发布服务器 5.4.0 和客户端 5.4.0。必须先部署服务器并完成健康检查，再分发客户端更新包，避免旧服务器不认识新版封号状态和客户端协议字段。

发布版本关系：服务器 5.4.0，客户端 5.4.0。

## 本版功能

- 后台将 OCR 管控和强制断开统一为“封号”，支持封 1 天、1 周、1 月、1 年、永久、自定义时长及解封。
- 被封客户端的授权、云端连接和 OCR 均受服务端状态约束。
- 客户端启动时先显示击杀画面，主 Web 画面稍后显示，降低直播采集到启动页的概率。
- 实时同步期间，场上姓名、比分、击杀、死亡、清空、重置、交换和历史回溯等编辑入口统一锁定，并显示“实时同步中”和红色禁止提示。
- 管理端保留主播、卡密、来源与 IP 地区信息；安装和打包不会自动执行任何真实冲突合并，冲突仍由管理员确认。

## 生产约定

- 服务器包：`dnf-cloud-match-server-production-5.4.0.zip`
- 客户端包：`update_v540.zip`
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

下面假定 ZIP 和 `.sha256.txt` 已上传到 `/root`。不能直接在 `/root` 执行 `deploy/install.sh`，脚本位于 ZIP 解压后的 `dnf-cloud-match-server/deploy` 目录。

```bash
cd /root
sha256sum -c dnf-cloud-match-server-production-5.4.0.sha256.txt

release_dir="/root/dnf-cloud-match-5.4.0-$(date +%Y%m%d-%H%M%S)"
install -d -m 700 "$release_dir"
unzip -q dnf-cloud-match-server-production-5.4.0.zip -d "$release_dir"
cd "$release_dir/dnf-cloud-match-server"

test -f deploy/install.sh
test -f deploy/preflight.sh
sha256sum -c SHA256SUMS.txt

sudo bash deploy/install.sh
sudo bash deploy/preflight.sh http://127.0.0.1:18880 /etc/default/dnf-cloud-match
bash deploy/preflight.sh http://47.109.149.111:18880
```

如果服务器缺少 `unzip`、Node.js 20+、npm、curl、CA 证书、Python 3 或原生模块编译工具，请在维护窗口前安装。安装脚本使用 `npm ci --omit=dev`，会按锁文件安装 `geoip-lite` 等运行时依赖，因此部署时需要能访问 npm registry。

## 部署顺序

### 1. 备份

1. 在维护窗口阻断新连接及其他数据库写入者。
2. 完整备份 `/opt/dnf-cloud-match-server`、`/var/lib/dnf-cloud-match`、`/etc/default/dnf-cloud-match` 和 systemd 单元。
3. 确认备份包含 SQLite、WAL/SHM、`.license-key`，并保留安装器生成的 `backup.complete` 标记。
4. 核对当前 `DATABASE_PATH`、自定义 systemd drop-in、文件所有者和权限。

### 2. 停止

安装器会停止并确认旧服务已退出后才复制文件。人工维护时可先执行：

```bash
sudo systemctl stop dnf-cloud-match.service
systemctl is-active dnf-cloud-match.service
```

第二条应返回 `inactive`。保持投稿和同步入口关闭，直到全部检查完成。

### 3. 安装

按“文件放在 /root 时的正确命令”完成校验、解压和 `deploy/install.sh`。安装器会串行升级、执行 `npm ci --omit=dev`、迁移程序文件，并保留生产数据库和环境配置。

已有 `ALLOW_LEGACY_PERMANENT_KEYS=false` 时必须保持原样；安装器不会自动把它改为 `true`。生产模板中的空管理员密码只是无密钥发布占位，部署时继续使用服务器现有安全配置。

### 4. 启动

安装成功后服务由安装器恢复。需要人工启动时执行：

```bash
sudo systemctl daemon-reload
sudo systemctl start dnf-cloud-match.service
systemctl is-active dnf-cloud-match.service
```

### 5. 健康检查

```bash
curl -fsS http://127.0.0.1:18880/health
curl -fsS http://127.0.0.1:18880/api/v2/health
curl -sS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:18880/api/v2/player-library
curl -fsS http://127.0.0.1:18881/admin/health
```

预期 `/health` 和 `/api/v2/health` 返回 200，未授权 `/api/v2/player-library` 返回 **401**。返回 **404** 表示仍是旧服务器或路由配置错误。确认公网 18880 可访问，管理端 18881 只监听回环地址。

不要用真实用户卡密 POST `/auth/activate` 做探针，这会产生或改变绑定。`/api/v2/health` 应报告 `protocolVersion: 2`、正确公网地址和实际兼容策略。

### 6. 管理员验证

管理端默认只监听回环地址。可从管理电脑建立 SSH 隧道：

```bash
ssh -L 18881:127.0.0.1:18881 root@47.109.149.111
```

然后访问 `http://127.0.0.1:18881/admin`：

1. 打开主播管理，确认当前 IP、地区和对应卡密可正常显示。
2. 打开密钥管理，确认已激活主播和设备关联正常。
3. 检查封号入口包含 1 天、1 周、1 月、1 年、永久、自定义和解封。
4. 使用测试设备短时封号，确认授权和 Socket 被拒绝；随后解封并确认可重新连接。
5. 检查共享选手库、待审投稿来源及现有冲突数量，确认无自动合并。
6. 确认数据数量正常后恢复入口流量，再发布客户端 5.4.0。

管理端使用 Basic Auth，生产公网目前仍是 HTTP。不要把 18881 直接暴露到公网；应使用 SSH 隧道或可信 TLS 反向代理。

## 回滚

如果安装或检查失败，继续阻断写入并保持服务停止。只选择带 `backup.complete` 的同一次备份，整体恢复其中的程序、完整数据目录、环境文件和 systemd 单元，包括 `.license-key`、WAL/SHM；不要只恢复一个 SQLite 文件，也不要重新生成保险库密钥来绕过错误。

恢复文件所有者和权限后执行 `systemctl daemon-reload`，再按备份记录恢复原来的 enable/active 状态。如果自动回滚输出 `MANUAL RECOVERY REQUIRED`，不要让旧代码连接可能已迁移的数据，先保存失败现场再人工恢复完整备份集。

## 本地打包验证

在仓库根目录执行：

```powershell
./scripts/package-production-5.4.0.ps1 -ValidateOnly
./scripts/package-production-5.4.0.ps1
```

打包器会在临时目录执行干净 TypeScript 编译，只允许 `dist/*.js`、部署脚本、锁文件和指定说明进入 ZIP，并生成包内 `SHA256SUMS.txt` 与 ZIP 的 SHA-256 旁车文件。它不会连接或修改生产服务器，也拒绝覆盖已有 5.4.0 产物。
