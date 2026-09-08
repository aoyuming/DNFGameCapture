# 移除冒险团ID：更新说明

## 本次变化

- 客户端只使用游戏ID识别；保留模糊匹配、多帧融合、大区和职业辅助判断。
- 删除冒险团后台扫描、识别明细、匹配兜底、编辑入口、同步字段和服务器审核字段。
- 同人自动归并只使用至少5个共同游戏ID；保留手动归并和别名。
- 选手、别名和游戏ID不删除。原先只配置冒险团ID的选手需要补充游戏ID后才能监控。
- 本地库升级到存储版本3，自动归并规则版本5；旧字段不再存储或参与运算。
- 服务器启动时执行一次事务清理。授权卡密、设备绑定、主播和比分保留。
- 本地旧自动组有完整原始快照时重新按游戏ID计算；依据不足或已编辑的组保留并提示复核。

## 客户端

更新文件放在仓库父目录的 `x64\Release`，包括 EXE、PDB 和 `web前端`。
不需要手动删除数据库，启动新版会在后台迁移。
不要用旧版直接打开已升级的数据库。需要回退时恢复成套程序和数据备份。

## 测试服部署

更新包：`deployment-packages/dnf-cloud-match-server-test-game-id-only-20260908.zip`。
这里只生成更新包，没有替换远端服务器。上传到服务器 `/root` 后执行：

```bash
set -e
cd /root
work="/root/dnf-test-game-id-only-$(date +%Y%m%d-%H%M%S)"
unzip /root/dnf-cloud-match-server-test-game-id-only-20260908.zip -d "$work"
sudo systemctl stop dnf-cloud-match-test
backup="/root/dnf-test-before-game-id-only-$(date +%Y%m%d-%H%M%S)"
sudo mkdir -p "$backup"
sudo cp -a /var/lib/dnf-cloud-match-test "$backup/data"
sudo cp -a /opt/dnf-cloud-match-server-test "$backup/app"
sudo cp -a /etc/default/dnf-cloud-match-test "$backup/environment"
echo "备份位置：$backup"
cd "$work/dnf-cloud-match-server"
sudo bash deploy/install-test.sh
curl -fsS http://127.0.0.1:28880/health
curl -fsS http://127.0.0.1:28881/admin/health
sudo journalctl -u dnf-cloud-match-test -n 40 --no-pager
```

只操作测试服 `28880/28881`，不更改正式服、OSS 或云函数。
部署后刷新已打开的管理网页，再运行新版客户端。旧投稿可以继续审核，无需重传。
若迁移报错，保留数据库和日志；不要靠删除投稿来跳过错误。
回滚需先停止测试服，再成套恢复旧程序、完整数据目录及配置，不能只覆盖运行中的 SQLite 主文件。
