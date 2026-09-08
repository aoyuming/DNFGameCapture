# 测试服旧永久卡兼容更新

本包包含此前的密钥管理、批量生成、加时、换绑和共享库修复，以及旧永久卡自动登记功能。只更新测试服 `28880 / 28881`，不修改正式服。已经使用新服务器授权接口的测试客户端无需更新 EXE，无需迁移 OSS 卡密文件。

## 旧卡如何使用

1. 用户继续输入原来的永久 CDK。
2. 服务器优先查询现有卡密记录。如果已有记录，仍检查绑定、禁用和到期状态，不覆盖管理员设置。
3. 尚无记录时，验证旧 `秘钥生成器/Keygen.cpp` 的永久卡格式和校验值。通过后自动保存为永久卡，绑定当前设备，签发授权会话。
4. 后台列表会出现备注“旧永久卡自动登记”，操作记录显示“旧永久卡登记”。可以查看原卡密、复制、换绑和禁用。

只自动接纳旧生成器的 `CDK-FFFFFFFF-四位十六进制随机值-八位十六进制校验值` 格式。未知日卡、月卡、签名错误、随意拼写或改变字段宽度的卡不会自动登记。新后台生成的卡仍必须先有服务器记录。

登记、绑定、保存卡密和签发会话为一次事务；失败不会留下半张卡。相同卡密重复验证不会重复登记，两台设备同时首次接入时只有一台能绑定。

## 更新测试服

上传 `dnf-cloud-match-server-test-legacy-permanent-license-20260907.zip` 到服务器，解压到一个新目录。在解压出来的 `dnf-cloud-match-server` 目录执行以下命令。先停止并备份完整测试数据、旧程序和配置，再安装：

```bash
set -e
sudo systemctl stop dnf-cloud-match-test
backup="/root/dnf-test-before-legacy-$(date +%Y%m%d-%H%M%S)"
sudo mkdir -p "$backup"
sudo cp -a /var/lib/dnf-cloud-match-test "$backup/data"
sudo cp -a /opt/dnf-cloud-match-server-test "$backup/app"
sudo cp -a /etc/default/dnf-cloud-match-test "$backup/environment"
echo "$backup"
sudo bash deploy/install-test.sh
curl -fsS http://127.0.0.1:28880/health
curl -fsS http://127.0.0.1:28881/admin/health
sudo grep -E '^[[:space:]]*ALLOW_LEGACY_PERMANENT_KEYS[[:space:]]*=' /etc/default/dnf-cloud-match-test
sudo journalctl -u dnf-cloud-match-test -n 40 --no-pager
```

安装脚本在缺少设置时自动加入 `ALLOW_LEGACY_PERMANENT_KEYS=true`；已经明确设为 `false` 的配置会保留。原管理员密码不变。后台：`http://47.109.149.111:28881/admin/licenses`。

## 配置与正式上线

`ALLOW_LEGACY_PERMANENT_KEYS=true` 启用旧永久卡自动登记；未配置或设为 `false` 时，只接受数据库已有的卡。已成功登记的旧卡不依赖这个开关，之后关闭它也能继续验证。

以后部署正式服，需要在正式服的独立配置中启用同一个开关并重启正式服务。本测试安装脚本不会替你修改正式服。也不需要把测试卡密、测试绑定或测试数据库整体覆盖到正式服。

旧版客户端若仍请求云函数，不会仅因服务器更新就自动改用新接口；需使用已经切到服务器授权的客户端。过渡期间不要误删旧云函数或旧 OSS 数据。

## 已确认的兼容边界

- 不读取旧 OSS，也不继承其绑定或封禁；谁先把旧卡用于新服务器，谁就获得新绑定。卡密曾被分享时可能被别人先绑定。此行为已由用户确认。
- 旧校验算法不能证明卡密确实曾由管理员发放。该兼容方案沿用旧算法的防伪能力，只适用于已确认的小范围旧卡过渡。
- 已在新后台禁用的卡不会重新登记。需要停卡时使用“禁用”，不要直接从数据库删除记录。
- 旧卡之前若已手工录入，原状态和绑定优先；需要更换设备时使用后台“换绑”。
- 旧后台曾接受带额外前导零的写法。如果数据库已经有同一卡密的另一种写法，服务器会拒绝另建永久卡，避免绕过原绑定或禁用；需继续使用后台原卡密或由管理员处理该记录。
- 换绑和禁用撤销服务端授权会话，但无法远程立即清除客户端已有的离线租约，也不是踢出主播操作。

## 备份

完整卡密加密保存在数据库中，随机加密文件位于数据库旁：`cloud-match-test.sqlite.license-key`。**数据库和该文件必须一起备份、迁移；不要删除或随意替换加密文件。** 修改管理员密码不会影响它。

当前后台为 HTTP，建议限制管理端口来源 IP 或使用 SSH 隧道。不要公开分享后台密码或导出的卡密文件。
