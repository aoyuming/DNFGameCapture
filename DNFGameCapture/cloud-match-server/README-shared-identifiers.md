# 测试服：共用游戏ID与自动归并

当前版本的迁移与回滚说明以 [README-game-id-only.md](README-game-id-only.md) 为准。

仅用于测试服 `28880 / 28881`。不需要更新云函数或OSS，不修改正式服。

## 新规则

- 不同选手允许共用游戏ID，投稿、审核和拉取不再因此失败。
- 去重后重叠游戏ID达到5个才自动归并；少量完全一致的ID也不例外。
- 归并取名称和游戏ID的并集，保留旧选手编号到新编号的映射。
- 低于阈值保持独立，名称归属冲突仍需处理。管理员可以另行确认同一人并手动归并。
- 原有待审核内容直接重新分析，无需删除或重传。
- 客户端只在当前8个位置中检查识别归属，场外同名ID不影响识别。
- 自动组保留原始归并依据，重复投稿和重启不会把合并后的并集当成新的依据；已删除ID不再参与判断。
- 不会根据“A像B、B像C”就把三者全部合并，组间原始成员也必须满足阈值。
- 已有手动合并保留。旧数据若没有完整归并前记录，无法可靠地自动拆回，需人工核对；不要为了重算而删除原库。

## 更新

先正常关闭旧客户端。上传本压缩包，解压到一个新目录；以下安装命令在解压出的 `dnf-cloud-match-server` 目录内执行。

本次包含SQLite表结构迁移。先停止测试服并备份完整数据目录及旧程序，不能只备份运行中的主数据库文件：

```bash
sudo systemctl stop dnf-cloud-match-test
backup="/root/dnf-test-before-shared-$(date +%Y%m%d-%H%M%S)"
sudo mkdir -p "$backup"
sudo cp -a /var/lib/dnf-cloud-match-test "$backup/data"
sudo cp -a /opt/dnf-cloud-match-server-test "$backup/app"
sudo cp -a /etc/default/dnf-cloud-match-test "$backup/environment"
echo "$backup"
sudo bash deploy/install-test.sh
curl -fsS http://127.0.0.1:28880/health
sudo journalctl -u dnf-cloud-match-test -n 40 --no-pager
```

服务启动时自动迁移，保留卡密、主播、公共库和待审数据。再启动新版客户端测试。

迁移后的数据库不能直接配旧服务端使用。需要回退时，停止测试服，成套恢复备份中的旧程序和完整数据目录。不要覆盖或删除正式服的目录。

管理入口：`http://47.109.149.111:28881/admin`。已有管理员密码不改变。

## 检查

1. 给两个不同选手添加一条相同ID，审核通过后仍显示两位。
2. 共享4个游戏ID时仍独立；补到5个游戏ID后归并。
3. 从其中一位移除共用ID，另一位仍保留。
4. 仅一位持有者上场时可识别；场上有多个无法区分的持有者时不得随机记分。
5. 重新打开客户端、重新投稿和重新拉取，归并结果及原有手动关系仍正确。
