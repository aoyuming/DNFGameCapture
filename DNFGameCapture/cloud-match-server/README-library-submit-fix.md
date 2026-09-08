# 测试服本地库投稿修复

以下保留旧投稿修复背景；当前发布规则及升级步骤以 [README-game-id-only.md](README-game-id-only.md) 为准。

## 修复内容

- 允许存在重叠游戏ID的本地库进入待审核区，不再在投稿阶段返回 `identifier_conflict`。
- 投稿成功仅表示服务器已保存待审核数据，不表示公共库已经更新。
- 后台“公共选手库审核”增加“查看投稿”，展示选手、游戏ID及名称冲突归属，可驳回投稿。
- 名称归属冲突按整位选手跳过并保留待审；自动归并要求至少5个共同游戏ID，且保留原始依据。
- 冲突投稿需先核对归属；可在客户端确认关联或修正游戏ID后重新投稿。仅有部分ID重叠不能推断一定是同一个选手。

当前客户端无需重装。本次不修改云函数、OSS、正式服配置或数据库。

## 更新已安装的测试服

将 `dnf-cloud-match-server-test-library-fix-20260906.zip` 上传到服务器的 `/root/`，执行：

```bash
unzip -o /root/dnf-cloud-match-server-test-library-fix-20260906.zip -d /root/dnf-library-fix
cd /root/dnf-library-fix/dnf-cloud-match-server
sudo bash deploy/install-test.sh
curl -fsS http://127.0.0.1:28880/health
```

安装程序仅更新 `dnf-cloud-match-test`，保留测试服数据库、卡密和管理员密码。
测试服 `28880` 会短暂重启，正式服 `18880` 不受影响。不要运行正式服安装命令。

更新后再次点击客户端“推送本地库”，成功时显示已提交审核区。
后台地址：`http://47.109.149.111:28881/admin`，刷新网页后点击“查看投稿”。
只对管理员自己的公网IP开放 `28881`，不要将后台开放给所有公网用户。

若仍失败，查看：

```bash
systemctl status dnf-cloud-match-test --no-pager
journalctl -u dnf-cloud-match-test -n 80 --no-pager
grep -n identifierConflictCount /opt/dnf-cloud-match-server-test/dist/v2-api.js
```

`WinHTTP 12002` 是连接或响应超时，和 `identifier_conflict` 数据冲突是两种不同问题。
本补丁修复已复现的数据冲突投稿拒绝，不保证修复外部网络中断。

## 本地验证

- 服务端单元/Socket/API 测试。
- TypeScript 源码构建和测试类型检查。
- 投稿详情浏览器检查：冲突禁用通过、驳回、轮询不关闭详情、HTML转义、桌面和窄屏窗口边界。
- 真实本地数据库仅只读打开，向本机内存服务器提交整份选手库，确认待审核保存成功且公共库不变；未向远程服务器上传测试数据。

此包只包含编译后的服务端、依赖清单和测试服安装文件，不含本地库、卡密、会话或数据库。
