# 服务器 5.2.3 登录会话关联修复包（2026-09-16）

本包包含服务器改动，不是客户端包。构建标识：`20260916-session-attribution-fix`。

## 修复内容

- 主播连接时，服务器验证客户端携带的卡密登录会话，再关联主播与对应卡密。
- 同一 IP 下有多个主播时，不再清除已通过登录会话确认的关联。
- 主播管理中显示“登录会话关联”。管理员仍可人工绑定。
- 旧客户端或无效会话继续使用原有 IP 关联规则，保持连接兼容。
- 启动时自动为原数据库补充关联标记字段，无需手工执行 SQL。

**先部署本服务器包，再更新本次修复后的客户端。** 旧客户端没有发送卡密会话，仅更新服务器不能保证所有历史“未关联卡密”记录立即补齐。使用新客户端登录并连接主播列表后，会自动关联；也可由管理员人工绑定。

## 上传到 /root 后部署

把下列两个文件上传到服务器的 `/root`：

- `dnf-cloud-match-server-production-5.2.3-session-fix-20260916.zip`
- `dnf-cloud-match-server-production-5.2.3-session-fix-20260916.sha256.txt`

服务器需已安装 Node.js 20+、npm、unzip、curl。安装过程中会按锁文件从 npm 安装 Linux 运行依赖；如原生模块需要编译，还需 Python 3 和 C/C++ 编译工具。

以 root 登录后，依次执行以下命令。必须先解压并进入包目录，不能直接在 `/root` 执行 `bash deploy/install.sh`。

```bash
cd /root
sha256sum -c dnf-cloud-match-server-production-5.2.3-session-fix-20260916.sha256.txt

release_dir=$(mktemp -d /root/dnf-cloud-match-session-fix-20260916.XXXXXX)
unzip -q dnf-cloud-match-server-production-5.2.3-session-fix-20260916.zip -d "$release_dir"
cd "$release_dir/dnf-cloud-match-server"
sha256sum -c SHA256SUMS.txt

bash deploy/install.sh
bash deploy/preflight.sh http://127.0.0.1:18880 /etc/default/dnf-cloud-match
bash deploy/preflight.sh http://47.109.149.111:18880
systemctl is-active dnf-cloud-match.service
```

预期安装和 preflight 均成功，服务状态为 `active`。健康检查不激活真实卡密；未授权访问 `/api/v2/player-library` 应返回 401。

## 数据与故障检查

安装器会先停止旧服务，备份程序、完整数据目录、环境文件和服务配置，再安装并启动新服务。现有卡密、选手库、投稿和人工关联保留。

- 程序：`/opt/dnf-cloud-match-server`
- 数据：`/var/lib/dnf-cloud-match`
- 配置：`/etc/default/dnf-cloud-match`
- 升级备份：`/var/backups/dnf-cloud-match/upgrade-*`

数据库旁的 `.license-key` 必须随数据库及 WAL/SHM 一起保留。安装失败时，安装器会尝试自动回滚；如果报告 `MANUAL RECOVERY REQUIRED`，请保留完整输出和备份，不要删除数据目录或重新生成密钥。

查看服务错误：

```bash
systemctl status dnf-cloud-match.service --no-pager -l
journalctl -u dnf-cloud-match.service -n 80 --no-pager
```

部署完成后，用本次新客户端登录并连接主播列表，再到管理员页面检查该主播的对应卡密和“登录会话关联”来源。名称别名弹层属于客户端功能，需要同时更新客户端程序和 `web前端` 文件夹。

## 本地重新打包

```powershell
./scripts/package-production-5.2.3.ps1 -SessionAttributionFix
```

打包会执行干净的 TypeScript 编译，校验压缩包内容并生成 SHA-256 校验文件。发布包不包含数据库、管理员密码或 `.license-key`。同名包已存在时，使用 `-OutputDirectory` 指定新目录。
