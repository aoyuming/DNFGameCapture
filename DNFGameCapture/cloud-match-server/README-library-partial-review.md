# 测试服：部分通过与公共选手合并

当前版本的迁移与回滚说明以 [README-game-id-only.md](README-game-id-only.md) 为准。

仅用于测试服 `28880 / 28881`。不修改正式服务、云函数或 OSS。

## 使用方式

- 后台共享库审核可以通过无冲突部分。按整位选手跳过归属冲突，不拆开该选手的名称和游戏ID；跳过部分继续保留待审核，原投稿留底。
- 确认两条公共记录是同一个人时，使用公共选手合并操作。先查看合并预览，再明确确认。即使游戏ID不同也能合并；名称和游戏ID取并集，不丢弃原记录的名称和游戏ID。
- 合并会保留旧编号到现编号的映射。新旧编号的投稿都指向现编号；合并本身不代表已通过该投稿的新内容，合并后重新查看并审核投稿。
- 新客户端拉取公共库时识别旧编号映射。仍存在归属冲突的选手单独跳过，其他选手正常合并；运行日志显示数量和前20位冲突选手。冲突不删除本地资料，也不会重置比赛比分或战绩。
- 全部冲突时没有数据会被强制通过。无效格式、过期版本或数据库写入失败仍会整体回滚。

## 更新顺序

先备份并更新测试服务器，再关闭客户端并替换新版客户端。无需删除、清空或重新上传现有待审核投稿。

将 `dnf-cloud-match-server-test-partial-review-20260907.zip` 上传到 `/root/dnf-cloud-match-install-test/`。服务器终端依次执行，任何一步报错都不要继续：

```bash
cd /root/dnf-cloud-match-install-test
unzip -o dnf-cloud-match-server-test-partial-review-20260907.zip -d partial-review-release
cd partial-review-release/dnf-cloud-match-server
sha256sum -c SHA256SUMS.txt
systemctl stop dnf-cloud-match-test
tar -czf "/root/dnf-cloud-match-test-backup-$(date +%Y%m%d-%H%M%S).tar.gz" /var/lib/dnf-cloud-match-test /etc/default/dnf-cloud-match-test /opt/dnf-cloud-match-server-test
bash deploy/install-test.sh
```

备份失败时先运行 `systemctl start dnf-cloud-match-test` 恢复旧服务，不要继续安装。

```bash
curl -fsS http://127.0.0.1:28880/health
curl -fsS http://127.0.0.1:28881/admin/health
systemctl status dnf-cloud-match-test --no-pager
```

打开 `http://47.109.149.111:28881/admin`，进入管理共享库并刷新。现有账号密码不变；仅首次安装或空密码时使用 `admin / Aym724794`。请将管理端口的安全组访问来源限制为自己的公网IP。

安装自动新增映射和审核留底表，保留原数据库。不要用旧数据库备份覆盖更新后的运行数据库；需要回滚时先停止测试服务，完整恢复备份的测试服务程序、数据库和配置。
