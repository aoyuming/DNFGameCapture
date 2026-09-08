# 共享库自动刷新与投稿去重

## 更新内容

- 共享库管理网页在前台自动检查更新；新投稿、审核结果和公共库变化无需手动刷新。
- 自动更新保留搜索条件、选择和未保存草稿。其他管理员修改了同一份数据时，仍执行版本校验，不覆盖草稿或跳过审核确认。
- 与公共库相比没有新增内容的投稿不再创建待审核记录。
- 同一设备的重复待审核投稿不再重复入队。
- 新客户端对已确认提交的相同内容跳过上传；网络失败不会记为成功。七天同步仍会拉取公共库。
- 包含上一版移除冒险团ID的改动；保留选手、别名、游戏ID、卡密、设备绑定和比赛数据。

## 更新测试服

此包仅用于测试服 `28880/28881`，不会自动部署到远端。上传 ZIP 到 `/root` 后执行：

```bash
set -e
cd /root
work="/root/dnf-test-library-auto-refresh-$(date +%Y%m%d-%H%M%S)"
unzip /root/dnf-cloud-match-server-test-library-auto-refresh-20260908.zip -d "$work"
sudo systemctl stop dnf-cloud-match-test
backup="/root/dnf-test-before-library-auto-refresh-$(date +%Y%m%d-%H%M%S)"
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

首次更新后，将已经打开的后台网页重新打开一次，以载入新的脚本。之后由网页自动刷新数据。
不需要重新部署云函数或 OSS。不会自动删除历史待审核投稿。

如需回退，先停止测试服务，成套恢复备份的程序、完整数据目录及环境配置，再启动服务。
不要使用旧程序直接打开新版迁移后的数据库，也不要只复制运行中的 SQLite 主文件。
