# DNFGameCapture 项目说明

这是一个面向 DNF 擂台/赛事场景的自动人头记分工具。桌面端负责捕获游戏或摄像头画面、识别死亡 X、调用 OCR、匹配击杀双方并写出 OBS 可读取的 TXT；WebView2 前端负责给用户操作红蓝队、选手、小号库、云端同步和最近识别复盘。

## 项目结构

- `DNFGameCaptureDlg.cpp/.h`：主窗口和核心业务。包含授权校验、画面捕获调度、死亡 X 检测、OCR 调用、击杀匹配、战绩统计、配置读写、WebView2 消息处理、小号库本地/云端同步。
- `WebScoreDlg.cpp/.h`：WebView2 记分窗口。加载 `web前端/index.html`，负责 C++ 与前端的双向消息桥接。
- `web前端/`：用户主要操作界面。包含红蓝队 4v4 记分、小号绑定/编辑、云端库同步/推送、最近识别、C++ 日志面板等。
- `WGCCapture.cpp/.h`：Windows Graphics Capture 捕获引擎，优先用于低闪烁、高性能窗口捕获。
- `CameraCapture.cpp/.h`：摄像头采集模式，用于外部采集卡/摄像头画面。
- `NameMatcher.hpp`：OCR 文本与小号/角色名的模糊匹配逻辑，包含常见 OCR 错字处理。
- `TemporalIdentityMatcher.hpp`：固定红框身份融合匹配。把 ID、大区、职业等多帧 OCR 证据合并，降低短 ID、符号 ID 和职业帧误判。
- `DNFGameCaptureDlg_IdentityPatch.cpp`：身份融合相关辅助实现，接入主窗口的玩家与小号数据。
- `云函数/`：Node/Express 云端服务，使用阿里云 OSS 做 CDK 授权、公共小号库、投稿审核区和管理员直写。
- `秘钥后台管理/`：共享小号库后台管理工具，包含 CLI、网页后台和打包说明。
- `秘钥生成器/`：CDK 生成器。
- `res/`：MFC 图标和资源文件。

## 核心流程

1. 用户在 WebView2 前端填写红队/蓝队选手和小号。
2. C++ 主程序通过 WebView2 消息接收前端状态，并同步到 `m_players` 和本地小号库。
3. 开始监控前会检查授权、Umi-OCR 服务、小号完整性和死亡 X 算法配置。
4. 捕获引擎从 DNF 窗口、指定窗口或摄像头取帧：
   - 自动/WGC：优先走 `WGCCapture`。
   - 兼容模式：WGC 不可用时降级到传统窗口捕获。
   - 摄像头模式：走 `CameraCapture`。
5. `CheckColorTrigger` 检测死亡 X。当前有两种算法：
   - `大X颜色个数判断`：按大 X 红色/橙色特征点判断。
   - `打补丁红蓝判断`：依赖补丁文件 `sprite(击杀大XX).NPK`，用红蓝点特征判断。
6. 触发后回溯历史帧并调用 Umi-OCR，读取固定区域中的 ID、大区、职业等文字。
7. `NameMatcher` 和 `TemporalIdentityMatcher` 综合 OCR 文本、小号元信息、队伍位置和时间窗缓存，确认击杀者/死亡者。
8. 程序更新击杀、死亡、AK 和总比分，广播状态给 Web 前端，并写出 OBS 可读取的 TXT 文件。

## 重要数据文件

这些文件通常位于 EXE 同目录或用户选择的输出目录：

- `players_config.txt`：本地选手、队伍、战绩和小号配置。
- `config.ini`：程序设置，如输出目录、捕获引擎、死亡 X 算法、`ImagePacks2Path` 缓存。
- `license.txt`：本机授权/CDK 记录。
- `alias_cloud_baseline.json`：云端共享小号库同步基线，用于判断本地库是否有新增/删除变化。
- `match_debug.log`：匹配和识别调试日志。
- `比分.txt`：OBS 总比分文本。
- `左侧人头.txt`：OBS 左侧队伍人头/死亡/AK 文本。
- `右侧人头.txt`：OBS 右侧队伍人头/死亡/AK 文本。
- `击杀.txt`：OBS 左右对齐的击杀简表。

## WebView2 前端消息重点

前端通过 `window.chrome.webview.postMessage` 发命令给 C++，C++ 再通过 `BroadcastStateToWeb` 或 `SendStateToWeb` 回推状态。常见命令包括：

- `cmd_monitor`：开始/停止监控。
- `cmd_swap`：翻转红蓝显示。
- `cmd_set_death_algorithm`：切换死亡 X 算法。
- `cmd_auth`：输入 CDK 授权。
- `cmd_sync_alias_db`：从云端公共库同步小号。
- `cmd_push_alias_db`：提交本地小号库到云端待审核。
- `cmd_direct_sync_alias_db`：管理员直写公共小号库。
- `cmd_delete_alias`：永久解绑小号。
- `cmd_reset_stats`：重置战绩。

## 小号库规则

- 主号只用于展示和归属；OCR 匹配主要使用小号。
- 开始监控前，上场选手必须至少有一个小号。
- 2 字短 ID 如果没有大区或 `#职业`，容易误识别，程序会阻止开始监控。
- 推荐格式：
  - `真实ID`
  - `跨区/大区 + 真实ID`
  - `真实ID#职业`
- 同一真实 ID 的不同职业写法会按 `#职业` 区分；无职业版本可能被更完整的职业版本覆盖。

## 云端与后台

云函数默认监听 `9000`，使用 OSS Bucket `dnf-capture-update`。主要数据路径：

- `licenses/`：CDK 绑定和到期信息。
- `shared-alias/public_alias_db.json`：公共小号库。
- `shared-alias/admin_key_hashes.json`：管理员直写白名单。
- `alias-submissions/pending/`：用户提交的待审核小号库变更。
- `alias-submissions/approved/`、`alias-submissions/rejected/`：审核归档。
- `alias-submissions/rejected-blocks/`：已驳回投稿的短期重复拦截。

后台管理在 `秘钥后台管理/`，支持网页后台和命令行工具。不要把 OSS AccessKey 写进公开前端页面；本地 exe 或服务端进程读取 `admin.config.json`/环境变量更安全。

## 开发注意点

- 工程是 MFC/Win32 C++ 项目，使用 C++17、WebView2、WinHTTP/WinInet、GDI/GDI+、WGC/Direct3D 相关能力。
- 当前仓库里部分旧文件可能有中文编码显示问题；新增文档请保持 UTF-8。
- `DNFGameCaptureDlg.cpp` 很大，改动前先用 `rg` 定位相关函数，避免误改相邻逻辑。
- Web 前端状态不是单独权威源，C++ 会回推完整状态；修改小号、选手和战绩时要同时考虑 C++ 与 Web 的同步方向。
- 死亡 X 补丁算法需要 EXE 同目录存在 `sprite(击杀大XX).NPK`，并复制到 DNF 的 `ImagePacks2` 目录。游戏运行中复制后通常需要重上游戏才生效。
- WGC 捕获失败时会降级兼容模式；不要在 WGC 正在运行时重复初始化或直接删除实例。
- Umi-OCR 不可用时会阻止开始监控，避免死亡 X 触发后 OCR 原文为空。
