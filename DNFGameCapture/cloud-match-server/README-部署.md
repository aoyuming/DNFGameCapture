# DNF 云端比赛同步服务器部署说明

适用系统：Ubuntu 22.04 64 位
默认端口：TCP `18880`
服务器地址：`http://47.109.149.111:18880`

本部署包使用简单的公网 HTTP 连接，不需要域名、Nginx 或 HTTPS 证书。比赛数据会以明文在公网传输，请不要在这个服务中保存密码、授权码等敏感信息。

## 一、上传压缩包

在 Windows PowerShell 中进入压缩包所在目录，然后执行：

```powershell
scp .\dnf-cloud-match-server-ubuntu22.04-x64.zip root@47.109.149.111:/root/
```

第一次连接时输入 `yes`，然后输入服务器的 root 密码。也可以使用 FinalShell、Xftp 或宝塔面板上传到 `/root`。

## 二、开放阿里云安全组

在阿里云控制台找到这台服务器，进入“安全组”并添加入方向规则：

| 协议 | 端口 | 来源 |
|---|---:|---|
| TCP | 18880 | `0.0.0.0/0` |

SSH 使用的 TCP `22` 也必须保持开放。程序端口直接对外使用，不需要开放 `80` 或 `443`。

## 三、一键安装

登录服务器：

```powershell
ssh root@47.109.149.111
```

在 Ubuntu 中执行：

```bash
apt-get update
apt-get install -y unzip
rm -rf /root/dnf-cloud-match-install
mkdir -p /root/dnf-cloud-match-install
unzip -o /root/dnf-cloud-match-server-ubuntu22.04-x64.zip -d /root/dnf-cloud-match-install
cd /root/dnf-cloud-match-install/dnf-cloud-match-server
chmod +x deploy/install.sh
./deploy/install.sh
```

脚本会自动完成：

- 安装 Node.js 22 和 SQLite 原生模块编译依赖。
- 将程序安装到 `/opt/dnf-cloud-match-server`。
- 将数据库保存在 `/var/lib/dnf-cloud-match/cloud-match.sqlite`。
- 创建 `dnf-cloud-match` systemd 服务。
- 设置开机启动并立即启动服务。
- 如果 UFW 已启用，自动开放 TCP `18880`。

重复运行安装脚本可以更新服务，数据库不会被覆盖。

## 四、检查是否成功

在 Ubuntu 中检查：

```bash
systemctl status dnf-cloud-match --no-pager
curl http://127.0.0.1:18880/health
```

在 Windows PowerShell 中检查公网连接：

```powershell
Invoke-RestMethod http://47.109.149.111:18880/health
```

能返回健康状态 JSON 就说明部署成功。

## 五、打开服务器管理后台

管理后台监听 `18881`，使用 HTTP Basic 登录保护。安装脚本首次部署时会生成随机管理密码，并在安装完成后显示。账号固定为 `admin`。

在阿里云安全组中添加 TCP `18881` 入方向规则，来源建议填写管理电脑当前公网 IP，不要使用 `0.0.0.0/0`。然后在浏览器打开：

```text
http://47.109.149.111:18881/admin
```

浏览器弹出登录框后输入账号 `admin` 和安装脚本显示的管理密码。忘记密码时在服务器执行：

```bash
grep '^ADMIN_PASSWORD=' /etc/default/dnf-cloud-match
```

后台可以查看在线和最近离线主播、比赛快照、实时同步关系与 24 小时同步记录，并可执行强制断开、停止实时关系、删除离线大厅数据和清理测试数据。后台不提供直接修改比分功能，也不会显示设备令牌、授权码、截图或外观配置。

## 六、软件连接方式

新版客户端不再显示房间选择和服务器地址输入框。授权成功后，客户端会使用授权接口下发的云端地址；旧授权响应则自动使用：

```text
http://47.109.149.111:18880
```

首次联网时只需填写主播名称，之后会进入统一在线主播大厅。当前在线名称不能重复。

为了方便在一台电脑上测试，首个客户端使用正式身份，第二个及后续客户端使用临时测试身份。临时实例关闭或断线超过约 2 秒后，服务器会自动删除它的主播信息、快照和同步记录，不进入 24 小时离线列表。

## 七、常用维护命令

```bash
# 实时查看日志
journalctl -u dnf-cloud-match -f

# 重启服务
systemctl restart dnf-cloud-match

# 停止服务
systemctl stop dnf-cloud-match

# 启动服务
systemctl start dnf-cloud-match

# 查看最近 100 行日志
journalctl -u dnf-cloud-match -n 100 --no-pager
```

环境配置文件位于 `/etc/default/dnf-cloud-match`。修改后执行：

```bash
systemctl restart dnf-cloud-match
```

## 八、备份与恢复数据库

停止服务后备份数据库：

```bash
systemctl stop dnf-cloud-match
cp /var/lib/dnf-cloud-match/cloud-match.sqlite /root/cloud-match-backup.sqlite
systemctl start dnf-cloud-match
```

恢复时先停止服务，再用备份文件覆盖原数据库，并修复所有者：

```bash
systemctl stop dnf-cloud-match
cp /root/cloud-match-backup.sqlite /var/lib/dnf-cloud-match/cloud-match.sqlite
chown dnfcloud:dnfcloud /var/lib/dnf-cloud-match/cloud-match.sqlite
systemctl start dnf-cloud-match
```

## 九、无法连接时

依次检查：

```bash
systemctl is-active dnf-cloud-match
ss -lntp | grep 18880
curl http://127.0.0.1:18880/health
curl http://127.0.0.1:18881/admin/health
ufw status
```

如果服务器本机能访问、Windows 不能访问，通常是阿里云安全组没有开放对应端口，或云服务器还有额外防火墙规则。公网后台请仅允许管理电脑的公网 IP 访问 `18881`。

## 十、先部署完全隔离的测试服

测试服不会读取或修改正式服的数据库、环境文件和 systemd 服务。它使用：

| 项目 | 正式服 | 测试服 |
|---|---|---|
| 比赛端口 | 18880 | 28880 |
| 管理端口 | 18881 | 28881 |
| 服务名 | `dnf-cloud-match` | `dnf-cloud-match-test` |
| 数据库 | `/var/lib/dnf-cloud-match/cloud-match.sqlite` | `/var/lib/dnf-cloud-match-test/cloud-match-test.sqlite` |
| 环境文件 | `/etc/default/dnf-cloud-match` | `/etc/default/dnf-cloud-match-test` |

把 `dnf-cloud-match-server-test-20260906.zip` 上传到服务器后执行：

```bash
mkdir -p /root/dnf-cloud-match-install-test
unzip -o /root/dnf-cloud-match-server-test-20260906.zip -d /root/dnf-cloud-match-install-test
cd /root/dnf-cloud-match-install-test/dnf-cloud-match-server
chmod +x deploy/install-test.sh
sudo ./deploy/install-test.sh
```

测试服健康检查：

```bash
curl http://127.0.0.1:28880/health
curl http://127.0.0.1:28881/admin/health
systemctl status dnf-cloud-match-test --no-pager
```

测试服后台地址是 `http://<服务器IP>:28881/admin`。请只给自己的公网 IP 开放 28881；28880 需要允许测试客户端访问。测试服的授权卡在后台“授权卡管理”生成，普通客户端投稿会进入“公共选手库审核”，审核后才会出现在公共库。

测试服继续使用简单的 HTTP，不具备 HTTPS 的传输加密；`28881` 只应对管理员自己的 IP 开放，勿把后台端口暴露给公网。测试安装脚本会为测试服务创建独立的 `dnfcloud-test` 系统账号，避免测试进程直接读取正式服务的数据目录。

把 `deploy/endpoint-manifest.test.json` 放到测试 OSS 地址时，客户端只在本地缓存地址连接失败后读取它。授权失败不会触发 OSS 回源，也不会绕过授权。生产 OSS、生产端口和旧云函数在测试阶段均不需要修改。

## 十一、v2 API 摘要

测试客户端使用以下接口：

```text
POST /api/v2/auth/activate
POST /api/v2/auth/validate
GET  /api/v2/player-library
POST /api/v2/player-library/resolve
POST /api/v2/player-library/submit
```

授权响应中的 `cloudServerUrl` 是唯一应缓存的比赛服务器地址；客户端不应从卡密、源码或旧固定地址推断新的测试端口。
