# DNF点将工具（DNFGameCapture）项目说明

当前版本：**5.5.6**（EXE FileVersion / ProductVersion `5.5.6.0`，前端缓存标识 `20260930-5.5.6-2`）

面向 DNF 擂台 / 赛事直播的自动人头记分与主播工具。

- **桌面端（MFC C++）**：捕获游戏或采集卡画面，检测死亡 X 和「红队胜 / 蓝队胜」横幅，调用 Umi-OCR 识别击杀双方，统计战绩，写出 OBS 用 TXT，驱动击杀特效、全屏特效和语音播报。
- **WebView2 前端（`web前端/`）**：红蓝队 4v4 记分、选手与游戏ID库、主播工具（展示 / 特效 / 声音 / 场景规则 / 情景状态）、云端同步、最近识别复盘、日志面板。
- **云服务器（`cloud-match-server/`）**：授权与设备绑定、选手库 v2、主播实时同步、云端音色和 TTS 配额，以及管理后台。

> 本文末尾的 **「已废弃 / 已移除」** 一节列出了不要再使用、不要再恢复的旧机制、旧文件和旧接口。改代码前请先看一遍。

---

## 1. 目录结构

| 路径 | 说明 |
|---|---|
| `DNFGameCaptureDlg.cpp/.h` | 主窗口与核心业务：授权、捕获调度、死亡 X / 队胜横幅检测、OCR、击杀匹配、战绩、场景规则执行、配置读写、WebView2 消息处理。文件很大，改动前先用 `rg` 定位。 |
| `WebScoreDlg.cpp/.h` | WebView2 记分窗口，加载 `web前端/index.html`，负责 C++ ↔ 前端消息桥。 |
| `KillDisplayDlg` / `KillFxDlg` / `KeyDisplayDlg` | 击杀展示窗口、全屏特效窗口、按键显示窗口（原生壳 + `kill.html` / `keys.html`）。 |
| `WGCCapture.cpp/.h` | Windows Graphics Capture 捕获引擎（首选）。 |
| `CameraCapture.cpp/.h` | 摄像头 / 采集卡模式。 |
| `NameMatcher.hpp` | OCR 文本与游戏ID的模糊匹配（含常见 OCR 错字）。 |
| `TemporalIdentityMatcher.hpp`、`PlayerIdentityOcrCache.h`、`DNFGameCaptureDlg_IdentityPatch.cpp` | 多帧身份融合：合并 ID、大区、职业证据，降低短 ID / 符号 ID 误判。 |
| `PlayerLibrary*.cpp/.h`、`PlayerIdentityGroupService.*` | 本地选手库（内嵌 SQLite）：实体、名称 / 别名、游戏ID、归并、迁移、推送策略。 |
| `AliasDbAutoSyncPolicy.*` | 游戏ID库七天自动同步策略。 |
| `CloudMatch*.cpp/.h`、`CloudReleasePolicy.*`、`LicenseLease.*` | 云端连接：服务器地址清单、正式 / 测试环境隔离、授权租约、主播实时同步。 |
| `VoiceCloudCache.h` | 云端音色目录与音频缓存。 |
| `HpBarCalibration*.h` | 血条百分比检测区域微调。 |
| `MutualDeathTriggerPolicy.h` | 同归于尽时对侧 OCR 补触发策略。 |
| `KeyMapping*.cpp/.h` | 全局按键映射与局域网按键服务。 |
| `OcrServiceHealth.h` | Umi-OCR 后台健康监测。 |
| `CrashDump.*` | 崩溃转储。 |
| `web前端/` | 前端：`index.html` / `main.js` / `style.css`（主界面），`kill.*`（展示页与特效），`keys.*`（按键页），`scene-rules*.js/.css`（场景规则），`scene-state-ui.js`（情景状态面板），`autocomplete-worker.js`。 |
| `cloud-match-server/` | Node + TypeScript + SQLite 服务器（端口 18880，管理端 18881；测试服 28880 / 28881）。部署说明见其中的 `README-*.md`。 |
| `scripts/` | 回归测试（`*.cjs` / `*.js` / `*.cpp` / `*.ps1`）、打包脚本 `package-production-client.ps1`、安装器 `installer/DNFGameCapture-setup.iss`。 |
| `docs/` | 各版本发布说明和专题文档（选手库、云端音色、血条校准、同归于尽触发等）。`docs/superpowers/` 是历史设计稿与执行计划，**仅供参考，不代表现状**。 |
| `third_party/sqlite/` | 静态编译进 EXE 的 SQLite。 |
| `用户指南.md` | 面向主播的操作说明。 |

---

## 2. 核心流程

1. 主播在前端填写红蓝队选手和游戏ID；C++ 同步到 `m_players` 和本地 SQLite 选手库。
2. 开始监控前检查：授权、Umi-OCR、上场选手均有游戏ID、2 字短 ID 必须带大区或 `#职业`、死亡 X 算法配置。
3. 捕获：自动 / WGC 优先 → 不可用时降级为兼容模式（DWM 捕获）；摄像头模式走 `CameraCapture`。
4. `CheckColorTrigger` 检测死亡 X，有两种算法：
   - `大X颜色个数判断`：每侧 8 个逻辑点，射线检测 X 两条斜边。
   - `打补丁红蓝判断`：需要 `sprite(击杀大XX).NPK` 复制到 DNF `ImagePacks2`（游戏运行中复制需重进游戏）。
5. 触发后回溯历史帧调用 Umi-OCR；`NameMatcher` + `TemporalIdentityMatcher` 结合游戏ID、大区、职业、座位与时间窗确认击杀者 / 死亡者。同归于尽时由 `MutualDeathTriggerPolicy` 在 1500 ms 窗口内补触发对侧。
6. 更新击杀 / 死亡 / AK / 连杀 / 一血 / 终结 / 复仇，执行**场景规则**（横幅 + 语音），广播状态给前端，写 OBS TXT。
7. **队胜横幅识别（5.5.6）**：在 2560×1440 基准下 12 个彩色点 + 10 个黑色点（红胜 / 蓝胜共用同一组坐标，仅彩色点期望颜色不同）。黑色点必须全中，彩色点 ≥ 8/12，且红蓝不打平，**命中 1 帧**即给对应队伍比分 +1，随后进入局间冷却。预览画面实时绘制识别框和每个点的命中状态。

---

## 3. 主播工具（5.5.3 起）

主操作栏的「主播工具」按钮，五个页签：

- **展示与输出**：展示窗口显示 / 隐藏，「txt输出选人顺序」（只影响 `击杀.txt` 的名字前缀）。
- **特效管理**：文字、击杀、全屏、事件、延迟、时长、大小。
- **声音管理**：默认静音、播报音色、缓存状态、试听（独立选择 9 种 UI 风格与音色，不修改直播设置）。
- **场景规则**：按优先级匹配的「条件 → 横幅 + 语音」规则。触发时机：击杀时 / 每局开始 / 比赛胜利 / 血量变化。条件可用击杀者 / 死者（含全部别名）、连杀、连死无人头、开局 / 剩余血量、复仇、赛点等。可设置「追加」播放、冷却。
  - 保存位置：`%APPDATA%\DNFGameCapture\scene_rules.json`；文件不存在时使用内置预设。
  - 「全部恢复默认」会恢复内置预设 **和** 自带专属情景（马区左侧 / 右侧白给、阿旺右侧白给、连死 4 次没人头、连死 4 次后终于拿人头）。
  - 内置预设默认值（5.5.6）：无伤击杀「打了个洞！」、逆风翻盘（开局 ≤40% 对满血 ≥90%，「残血翻盘了，有点厉害喔」）、赛点局「赛点了喔」默认开启并追加；七分胜利默认追加；残血反杀、残血预警示例默认关闭。
- **情景状态**：查看并**直接编辑**本局情景数据（击杀记录、连杀、连死无人头、一血、复仇等），没有数据时也能编辑；撤销 / 重做会同步。

战绩规则：

- **重置战绩**：视为新一轮比赛，清空全部情景数据，选人顺序一起重置。
- **手动改比分**：不弹窗，只重置本局数据（清空连杀、全员复活）；保留连死无人头、历史、一血、复仇。

---

## 4. 数据与配置文件

| 文件 | 位置 | 说明 |
|---|---|---|
| `player_library.db`（+ `-wal` / `-shm`） | 正式版 `%APPDATA%\DNFGameCapture\production\`；测试构建 `%APPDATA%\DNFGameCapture\test\` | **选手库唯一真实数据源**。见 [docs/local-player-library.md](docs/local-player-library.md)。 |
| `scene_rules.json` | `%APPDATA%\DNFGameCapture\` | 场景规则（`{ version, rules, aliases, gao }`）。 |
| `voice-cloud\<服务器哈希>\` | `%LOCALAPPDATA%\DNFGameCapture\` | 云端音色缓存，按服务器隔离。 |
| `voice-cache\` | `%APPDATA%\DNFGameCapture\` | Windows 本机语音离线缓存。 |
| `config.ini` | EXE 同目录 | 程序设置：输出目录、捕获引擎、死亡 X 算法、`ImagePacks2Path`、`[CloudMatch]` 设备信息与地址缓存、`[Upgrade520]` 迁移记录等。 |
| `players_config.txt` | EXE 同目录 | 场上选手、队伍、战绩。 |
| `license.txt` + 注册表 | EXE 同目录 / HKCU | 授权码，两处互为备份（普通启动以注册表优先）。验证成功后另存 5 天加密授权租约。 |
| `alias_db.ini` | EXE 同目录 | **只是兼容导出**，见第 9 节。 |
| `match_debug.log` | EXE 同目录 | 匹配与识别调试日志（含 `[队胜识别]` 诊断）。 |
| `比分.txt` / `左侧人头.txt` / `右侧人头.txt` / `击杀.txt` | 输出目录 | OBS 读取的文本。 |

---

## 5. WebView2 消息

前端用 `window.chrome.webview.postMessage` 发命令，C++ 用 `BroadcastStateToWeb` / `SendStateToWeb` 回推完整状态。**C++ 是权威状态源**，前端修改选手、游戏ID、战绩时要考虑双向同步。

常用命令：`cmd_monitor`（开始 / 停止监控）、`cmd_swap`（翻转红蓝）、`cmd_set_death_algorithm`、`cmd_auth`、`cmd_reset_stats`、`cmd_sync_alias_db`（拉取公共库）、`cmd_push_alias_db`（提交本地变更待审核）、`cmd_delete_alias`、`cmd_set_alias_auto_sync`。

`cmd_direct_sync_alias_db` / `cmd_set_alias_direct_mode` 已失效，见第 9 节。

---

## 6. 游戏ID库规则

- 选手名称只用于展示和归属，OCR 匹配只用**游戏ID**。
- 开始监控前，上场选手必须至少有一个游戏ID。
- 2 字短 ID 若没有大区或 `#职业`，会阻止开始监控。旧库中这类 ID 会标黄提示清理。
- 推荐格式：`真实ID`、`跨区/大区 + 真实ID`、`真实ID#职业`。
- 别名共用实体的游戏ID；自动归并要求**至少 5 个共同游戏ID**，手动归并保留。
- 云端使用 v2 `entities` 格式（`entityId` / `names` / `gameIds`），先拉取并落库，再从最新快照提交投稿；公共库变更由服务器后台审核。

---

## 7. 云端

- 正式版 EXE 内置地址清单 `https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json`，测试构建读取 `cloud-server-test.json`。之后复用同环境地址缓存，连接失败时才重读清单。
- 授权、选手库、实时同步、云端音色全部走 `cloud-match-server`（v2 接口 `/api/v2/*`）。
- 发布顺序必须 **server-first**：先部署服务器并通过健康检查（`/health`、`/api/v2/health` 返回 200，未登录访问 `/api/v2/player-library` 返回 401），再分发客户端。
- 管理后台（18881，仅回环 / SSH 隧道访问）：`/admin`、`/admin/licenses`、`/admin/library`（选手库，直接读写服务器 SQLite）、音色与 TTS 配额管理。
- 客户端不调用豆包 TTS，也不持有 API Key；付费生成只在服务器后台由管理员触发。

---

## 8. 构建、测试与发布

- 环境：VS2026，MFC / Win32，C++17，WebView2，`DNFGameCapture.slnx`。
- 正式构建：

  ```
  MSBuild DNFGameCapture.slnx /p:Configuration=Release /p:Platform=x64 /m
  ```

  测试服构建必须显式加 `/p:DnfCloudEnvironment=Test`，并使用单独的输出目录。
- 输出在解决方案根目录 `x64\Release\`（不是内层 `DNFGameCapture\x64\`）。
- **构建不会复制 `web前端/`**：改了前端要手动同步到 `x64\Release\web前端\`，并同时更新 `index.html`、`kill.html`、`main.js` 中的缓存版本号。
- 改版本号要同步：`DNFGameCaptureDlg.h` 的 `CURRENT_VERSION`、`DNFGameCapture.rc`（UTF-16 LE，FILEVERSION / PRODUCTVERSION 和字符串）、`scripts/package-production-client.ps1`、`scripts/streamer-tools-test.cjs`、`scripts/installer/DNFGameCapture-setup.iss`。用 `node scripts/streamer-tools-test.cjs` 检查是否一致。
- 已知的唯一编译警告：`fallback_printwindow` 未引用标签（C4102），可忽略。
- 常用回归：
  - `node scripts/streamer-tools-test.cjs`
  - `node scripts/kill-voice-feature-test.js`
  - `node scripts/left-victory-test.cjs`
  - `node --test scripts/player_library_web_test.js`
  - `scripts/check-player-library-core.ps1`
  - 服务器 `cd cloud-match-server && npm test`
  - 已知问题：`main-actions-test.cjs` 的版本断言失败是遗留问题。
- 安装包：`scripts/installer/DNFGameCapture-setup.iss`（AppId 不变，覆盖升级）。更新包只含白名单运行文件，不含配置、卡密、数据库、日志。
- 不要提交：`build/`、`x64/`、`deployment-packages/`、`pkg-staging-*`、`*.obj`、`match_debug.log`、`.vs/`、个人配置和授权文件。`web前端/voice/` 是本地语音资产，目前不在仓库中。

---

## 9. 已废弃 / 已移除（不要再用，不要恢复）

> 5.5.6 起已从仓库删除 `云函数/`、`秘钥后台管理/`、`秘钥生成器/`、`WebSyncSvc.h`，以及依赖云函数的测试 `scripts/alias_append_lock_test.js`、`scripts/alias_db_append_mode_test.js`、`scripts/check-alias-db-append-mode.ps1`。需要查看时可在 git 历史中找到（例如 `git show 09859e1:DNFGameCapture/云函数/index.js`，或 `git checkout 09859e1 -- DNFGameCapture/云函数`）。

### 9.1 云端与授权

| 废弃项 | 现状 / 替代 |
|---|---|
| **`云函数/`**（Node/Express + 阿里云 OSS：CDK 验证、公共游戏ID库、投稿审核、管理员直写）——**已删除** | 5.2.0 起被 `cloud-match-server` 取代。客户端里 `m_cloudServerAuthV2` 固定为 `true`，所有「非 V2」分支（直连云函数验证、OSS 扁平库同步 / 投稿 / 直写）都是**死代码**，仅为兼容保留。客户端源码中这些分支暂未清理。 |
| **`秘钥后台管理/`**（基于 OSS 的 CLI 与网页后台）——**已删除** | 被服务器后台 `/admin/licenses`、`/admin/library` 取代（数据在服务器 SQLite，不连 OSS）。 |
| **`秘钥生成器/`**（`Keygen.cpp`，生成 `CDK-FFFFFFFF-xxxx-xxxxxxxx` 永久卡）——**已删除** | 新卡一律在 `/admin/licenses` 生成。该格式的旧永久卡仍可使用：服务器 `ALLOW_LEGACY_PERMANENT_KEYS=true` 时首次验证自动登记并绑定设备。服务器 `cloud-match-server/src/auth.ts` 内置了该格式的校验。 |
| **更早的「旧版卡密」**（非上述格式） | 已淘汰，客户端直接提示「已淘汰的旧版卡密，请更换新版 CDK」。 |
| `config.ini` `[CloudMatch] ServerUrl` | 已废弃，启动时自动删除，不再读取。服务器地址只来自内置清单和同环境缓存（`LastKnownServerUrl`）。 |
| `config.ini` `[CloudMatch] ServerAuthV2` | 已废弃，正式版忽略。仅在 5.2.0 首次升级时用来判断旧库来源。测试环境改用 MSBuild `/p:DnfCloudEnvironment=Test`。 |
| 旧 Socket.IO 设备身份 / 旧房间字段 | 统一主播池取代旧房间模式。旧字段只用于迁移，可选身份字段不阻塞旧连接。 |
| `UPDATE_CHECK_URL_V1`（`update.txt`，第一版单 EXE 更新） | 已由 `update_v2.txt` + 更新 ZIP 取代。 |
| `alias_cloud_baseline.json` | 旧扁平库时期的云端基线，仅旧路径使用。v2 按请求地址记录云端版本水位。 |

### 9.2 选手库与识别

| 废弃项 | 现状 / 替代 |
|---|---|
| **`alias_db.ini` 作为数据源** | 5.2.0 起 SQLite 是唯一真实数据源。INI 只是**单向兼容导出**，手工编辑不会被导入，请在软件里编辑。 |
| `player_identity_groups.json` | 只在首次迁移时读取，迁移后不再使用（原文件保留，另有 `*.migrated.bak` 备份）。 |
| 扁平游戏ID库格式（`{players}` / `mainName → "id1,id2"`） | 被 v2 `entities` 取代。仍可导入，但不再作为同步格式。 |
| 扁平库单组 32 名称 / 256 ID 限制 | v2 已放宽到单组各 10,000 项。总请求和公共库仍限 256 KiB。 |
| **冒险团 ID**（扫描、识别兜底、编辑入口、同步与审核字段） | 已全部删除，只用游戏ID识别。本地库存储版本 3 和服务器启动迁移会清理旧数据，旧请求里的该字段会被忽略。只配了冒险团 ID 的选手必须补游戏ID。 |
| 旧自动归并规则 | 现为规则版本 5：至少 5 个共同游戏ID。 |
| 客户端管理员直写公共库（`cmd_direct_sync_alias_db`、`cmd_set_alias_direct_mode`） | V2 下只回提示「服务器选手库请通过后台审核管理」，直写模式无法开启。改为后台审核。 |
| 2 字短 ID 且无大区 / 职业 | 会阻止开始监控，旧库里的这类 ID 会提示清理。 |

### 9.3 捕获与检测

| 废弃项 | 现状 / 替代 |
|---|---|
| BitBlt 捕获 | 已废弃，兼容模式统一用 DWM 穿透捕获。 |
| `PrintWindow` 回退（`fallback_printwindow`） | 跳转已注释，标签残留（即 C4102 警告的来源）。 |
| `OnWGCInitDone` 异步回调 | 已废弃，函数体为空。不要在 WGC 运行中重复初始化或直接删除实例。 |
| 大 X 40 点「每组 5 点」逻辑 | 坐标保留 40 点只为兼容旧采点输出，实际只用每组第 0 个点（共 8 个逻辑点）。 |
| 旧版队胜识别（横幅区域颜色占比） | 误判多，5.5.6 改为 22 个固定采样点。 |
| 队胜横幅「连续 3 帧」判定 | 5.5.6 改为 1 帧。 |
| 队胜横幅「12 个彩色点全中」 | 5.5.6 改为彩色点 ≥8/12（黑色点仍须全中）。 |
| PaddleOCR v3 模型 | 用 `upgrade-ocr-v4.bat` 升级到 PP-OCRv4，v3 自动备份到 `models/backup-v3-*`。 |

### 9.4 前端与界面

| 废弃项 | 现状 / 替代 |
|---|---|
| 主操作栏「展示页面」「特效管理」两个按钮 | 5.5.3 合并为「主播工具」。 |
| 「更多 → 选人顺序」 | 改名为「txt输出选人顺序」，移到「展示与输出」。设置键不变。 |
| 石墨主题 `graphite-theme.css` 及两版主播状态列表设计预览 | 未采用（5.5.0）。安装器会主动删除残留的 `graphite-theme.css`。 |
| 旧版固定击杀播报逻辑（AK > 三杀 / 双杀 > 一血 > 终结 > 复仇 > 单杀） | 改由场景规则的内置预设实现，行为一致，可编辑。 |
| 修改比分时弹窗 | 5.5.6 已取消。 |
| 样式键 `statNumber` | 拆为 `killNumber` / `deathNumber`，旧键只作读取兜底。 |
| 客户端调用豆包 TTS 生成语音 | 已移除。客户端只播放随包 WAV、云端缓存，或 Windows 本机语音。 |
| 随包旧 WAV（已被服务器新版本替代的） | 哈希一致时导入缓存。已被替代的旧打包文件不再播放。 |

### 9.5 文档

- `docs/superpowers/plans|specs/`：历史设计与执行计划，内容可能已过时，以代码和本 README 为准。
- `cloud-match-server/README-production-5.2.0/5.2.2/5.2.3.md`：已被 `README-production-5.4.0.md` 及之后的部署说明取代，只作为回滚参考。
- `cloud-match-server/README-部署.md`：最早的一键部署说明（Node 22、端口 18880），授权与选手库相关内容以后续 production 文档为准。
- `WebSyncSvc.h`：未被任何源文件引用的遗留头文件——**已删除**。

---

## 10. 开发注意

- 新增文档统一用 UTF-8；`DNFGameCapture.rc` 是 UTF-16 LE（带 BOM），编辑时保持编码不变。`kill.js` 等文件使用 CRLF。
- 不要用测试库覆盖正式库，回滚时要成套恢复程序、配置、数据库（含 WAL / SHM）。
- 服务器部署脚本只操作对应环境：正式 `dnf-cloud-match`，测试 `dnf-cloud-match-test`。
- 正式公开服务建议使用 HTTPS，管理端只经 SSH 隧道访问，避免明文传输卡密。
