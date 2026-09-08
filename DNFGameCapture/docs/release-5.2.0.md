# 5.2.0 正式版升级

## 发布顺序

1. 先部署 `dnf-cloud-match-server-production-5.2.0.zip`。操作见 [服务器部署说明](../cloud-match-server/README-production-5.2.0.md)。
2. 检查正式服 `18880` 的 `/api/v2/health`，确认 `protocolVersion: 2`、`allowLegacyPermanentKeys: true`，且 `cloudServerUrl` 为 `http://47.109.149.111:18880`。
3. 未登录访问 `/api/v2/player-library` 应为 HTTP 401，不能是 404。
4. 用受控测试设备验证一张尚未绑定的新测试卡，再用一位主播自己的设备试升级和原永久卡验证；不要拿主播卡在其他机器上试，首次验证会绑定该机器。
5. 核对试升级设备的选手库后，再发布 `update_v520.zip`。目前只生成本地包，没有上传 OSS 更新包或改线上更新清单。

正式服务器旧程序不包含 v2 授权接口时，新客户端无法完成在线验证。不能先让主播全量更新客户端。

把服务器 ZIP 上传到服务器 `/root` 后，在维护时段执行：

```bash
cd /root
unzip dnf-cloud-match-server-production-5.2.0.zip -d dnf-cloud-match-install-production-5.2.0
cd dnf-cloud-match-install-production-5.2.0/dnf-cloud-match-server
sha256sum -c SHA256SUMS.txt
sudo bash deploy/install.sh
sudo bash deploy/preflight.sh http://127.0.0.1:18880 /etc/default/dnf-cloud-match
bash deploy/preflight.sh http://47.109.149.111:18880
```

安装脚本操作正式服 `dnf-cloud-match`，不操作测试服；会短暂停服并完整备份程序、数据、配置及卡密加密文件。安装失败会尝试整体回滚。执行前确认 `/usr/bin/node` 为 20 或更新版本，且已备份正式库；不要直接用测试库覆盖正式库。

## 地址与授权

- 正式 EXE 内置 OSS 地址清单 `https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json`，首次启动读取清单，之后复用同环境地址缓存或加密租约。连接失败才重新读取 OSS。
- 正式版不受旧 `ServerAuthV2=0` 或测试服清单配置影响。测试构建必须显式使用 MSBuild `/p:DnfCloudEnvironment=Test`，并单独指定构建输出目录。
- 原有永久 CDK 的格式、签名和机器码算法不变，不需要重新发卡。首次升级必须在线验证一次，成功后重新保存带环境标记的五天加密租约。
- 没有迁移 OSS 绑定记录时，未在新服登记的有效旧永久卡由首次使用的设备绑定。已经在新服禁用或绑定的卡仍受原限制；旧卡兼容不是解除所有绑定。
- 部署程序保留显式 `ALLOW_LEGACY_PERMANENT_KEYS=false`。要接收未登记的旧永久卡，发布前必须确认正式服已设为 `true` 并重启服务。
- 非旧 CDK 格式的任意字符串卡不在此次兼容范围内。

## 本地库保护

正式版使用 `%APPDATA%\DNFGameCapture\production\player_library.db`。

- 首次升级先在 `config.ini` 的 `[Upgrade520]` 记录升级前的库来源，并保留 `config.ini.before-5.2.0.bak`。
- 旧测试版用户从 `%APPDATA%\DNFGameCapture\test\player_library.db` 复制；普通旧版从 `%APPDATA%\DNFGameCapture\player_library.db` 复制。使用 SQLite 备份接口，包含已经提交到 WAL 的数据。
- 没有原 SQLite 数据库时，迁移原 EXE 同目录的 `alias_db.ini` 和 `player_identity_groups.json`。保留选手名称、别名、游戏ID及可用的身份组元数据。
- 游戏ID在新库存一份，再通过关联表共享；不同选手允许关联同一个游戏ID。已移除的冒险团字段不恢复。
- 原文件不会删除。迁移先在临时数据库进行，验证后才安装；损坏数据库不会降级为空库覆盖 INI。
- 已有正式数据库时直接使用，不在每次启动重复导入旧数据，因此主动删除的选手不会因为重启而复活。
- 后续编辑以 SQLite 为准，旧 INI 继续作为兼容导出。不要在迁移成功后用手工修改旧 INI 的方式编辑新库。

## 更新包与回退

`update_v520.zip` 是原目录覆盖升级包，不是完整 OCR 安装包。主播应在原软件目录升级，不要解压到空目录后删除原目录；全新目录无法自动找到旧便携版旁边的 INI。

更新包只含 EXE、WebView2Loader、7za、NPK 和十个前端文件，不含配置、授权、数据库、比赛数据或日志。服务端包也不含测试数据库，测试共享库只能通过后台的导出/导入流程迁移，不能复制整个测试数据库覆盖正式库。

回退前先退出软件，并备份当前正式数据库及其 WAL/SHM 文件。恢复旧 EXE、配套前端和升级前配置；旧测试/普通 SQLite 源库仍在原路径。INI 版本可使用升级时保留的 `.migrated.bak`。服务端请按部署说明整体恢复匹配的程序、配置、数据库和卡密加密文件，不要只回退 EXE 或服务代码后继续使用已迁移的新库。

当前清单使用 HTTP 服务器地址。正式公开服务建议配置 HTTPS，管理后台只经 SSH 隧道或受限网络访问，避免公网明文传输卡密及管理员会话。

## 本次验证

- Release x64 编译通过，EXE 文件版本为 `5.2.0.0`，已替换 `C:\Users\BRO\source\repos\DNFGameCapture\x64\Release` 的 EXE/PDB/前端。
- 服务器 24 个测试文件、326 项测试通过；包含旧永久卡登记/绑定、原记录保留、安装中断和回滚。
- 授权租约/正式与测试环境隔离、6 项客户端升级接线检查、本地核心库及 SQLite/WAL 迁移测试通过。
- 临时迁移真实 Release INI：130 个名称、1288 条引用、470 个唯一游戏ID，源文件 hash 未变。未对主播实际机器执行线上激活。
- 浏览器自动刷新 13 项测试通过；实际 Edge 后台 CRUD/导入/审核回归通过。
- 最终 ZIP 的 14 个文件逐一与 Release 校验一致，不含用户配置、卡密、库或比赛数据；替换前后 `config.ini` hash 一致。
- 替换前备份：`C:\Users\BRO\source\repos\DNFGameCapture\build\before-production-5.2.0-20260909-042538`。
- 未部署线上服务，未上传 OSS 发布更新，也未执行 Git 提交/推送。

SHA256：

```text
DNFGameCapture.exe
580100DD500250FE51E46569CE4574C50B898F8D00B525CD5F7B44625BA883C0
update_v520.zip
160BED4D817D272DE40A50DCEF7A8096FE8B43ED78CACDB57D66C16917FC213C
dnf-cloud-match-server-production-5.2.0.zip
D719CFCED2CDD5E8E61AA736D11501B28B3A3ED14A4C3EF1FB155C43FECF904E
```
