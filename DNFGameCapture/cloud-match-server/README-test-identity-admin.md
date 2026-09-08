# 测试服：后台独立入口与投稿身份对齐

当前版本的迁移与回滚说明以 [README-game-id-only.md](README-game-id-only.md) 为准。

本包只更新测试服：比赛端口 `28880`，管理端口 `28881`。不要用于正式服 `18880/18881`。

## 本次变更

- `/admin` 登录后只显示管理密钥、管理主播、管理共享库三个入口；沿用原管理账号密码。
- `/admin/licenses` 管理密钥，`/admin/broadcasters` 管理主播，`/admin/library` 保留三栏共享库管理台。
- 投稿仅通过已确认的旧编号映射对齐公共编号；自动归并在审核发布时基于至少5个共同游戏ID及原始依据检查，名称相同或少量ID集合一致不直接归并。
- 投稿区区分新增、补充、无变化与剩余冲突。既有待审核投稿重新读取即可分析，不需要删除重传；原始投稿不会因浏览而改写。
- 只有管理员确认审核通过后才写公共库；重复无变化内容不会提高公共库版本。
- 修改并保存带自动关联的投稿时，会先要求确认这些关联。首次编辑前的原始投稿留底保存，取消或失败不会改动数据；保存草稿仍不等于审核通过。
- 名称、别名和游戏ID直接保存到服务器 SQLite，不访问旧 OSS 选手库。
- 支持查看/修改投稿、冲突处理、审核/驳回、公共库编辑和导入；敏感写入有版本校验。
- 旧比赛快照清理已删除的身份字段后仍可读取；比分、名称、别名和战绩不变。
- 公共选手库发布时至少5个共同游戏ID才自动归并；保留原始依据以防止重复投稿扩大归并范围。
- 归并后的选手库身份组不再受旧的32名称/256ID限制，单类列表上限为10,000项；选手库请求/公共库仍受256 KiB总大小限制。比赛快照限制不变。

## 更新步骤

将 `dnf-cloud-match-server-test-admin-hub-20260907.zip` 上传到 `/root/dnf-cloud-match-install-test/`，然后在服务器终端依次运行。前一步报错时不要继续：

```bash
cd /root/dnf-cloud-match-install-test
unzip -o dnf-cloud-match-server-test-admin-hub-20260907.zip -d admin-hub-release
cd admin-hub-release/dnf-cloud-match-server
sha256sum -c SHA256SUMS.txt
systemctl stop dnf-cloud-match-test
tar -czf "/root/dnf-cloud-match-test-backup-$(date +%Y%m%d-%H%M%S).tar.gz" /var/lib/dnf-cloud-match-test /etc/default/dnf-cloud-match-test /opt/dnf-cloud-match-server-test
bash deploy/install-test.sh
```

只有测试服务短暂停止。安装脚本保留现有测试服数据库、卡密和管理密码；不会修改正式服务。
如果备份或解压报错，请先停止后续命令，并用 `systemctl start dnf-cloud-match-test` 恢复旧测试服。
环境要求与之前相同：Ubuntu 22.04、Node.js 20 或更高版本。

## 检查

```bash
curl -fsS http://127.0.0.1:28880/health
curl -fsS http://127.0.0.1:28881/admin/health
systemctl status dnf-cloud-match-test --no-pager
journalctl -u dnf-cloud-match-test -n 50 --no-pager
```

打开 `http://47.109.149.111:28881/admin`，使用原测试服后台账号和密码，再选择对应入口。浏览器若已记住登录信息，会直接显示三个入口。

首次安装的默认账号为 `admin`，默认密码为 `Aym724794`。已有环境文件中的非空密码会保留，升级不会重置；密码为空时填入该默认密码。需要修改现有测试服密码时，编辑 `/etc/default/dnf-cloud-match-test` 的 `ADMIN_PASSWORD`，然后重启 `dnf-cloud-match-test` 服务。

管理端口目前为 HTTP，请将云防火墙/安全组的 `28881` 访问来源限制为自己的公网IP，不向所有人开放。

## 客户端顺序

先更新测试服务器，再关闭并更新参与测试的客户端。已安装上一包客户端时，本次客户端操作栏与游戏ID标签编辑仅需更新前端文件；保留原 EXE、`config.ini` 和用户数据库。从更旧版本升级仍需整套客户端。
客户端仍使用测试模式配置，不要把测试地址写到正式服配置。参与测试的客户端均应升级到仅使用游戏ID的版本。

测试时分别验证：一次同步、实时同步、点击“关联”、阈值归并、已忽略的关系、后台投稿查看与审核。
自动归并不改写场上的选手显示名称、比分或战绩。完整本地库不截断，完整库同步仍走选手库接口。

不需要重新部署云函数、旧 OSS 管理网页或更新正式比赛服务器。
