# 测试服授权与选手库迁移实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变生产端口、生产数据库和旧云函数行为的前提下，为测试服增加服务器授权、规范化选手库、管理接口和客户端地址缓存基础闭环。

**Architecture:** 旧 Socket.IO 比赛同步协议继续保留；新增 `/api/v2` HTTP API 使用独立 SQLite 表保存授权、设备会话、选手实体、名称、游戏 ID 和冒险团 ID。测试部署使用 28880/28881、独立数据库和 systemd 服务；客户端先使用 DPAPI 租约中的地址，只有连接失败才从 OSS 读取 endpoint manifest。

**Tech Stack:** Node.js、TypeScript、Express、Socket.IO、SQLite/better-sqlite3、Vitest、WinHTTP、C++ DPAPI。

---

### Task 1: 服务器纯函数核心

**Files:**
- Create: `cloud-match-server/src/player-library.ts`
- Create: `cloud-match-server/src/auth.ts`
- Test: `cloud-match-server/tests/player-library.test.ts`
- Test: `cloud-match-server/tests/auth.test.ts`

- [ ] 先写规范化、唯一归属和“游戏 ID 优先于冒险团 ID”的失败测试。
- [ ] 先写授权卡密哈希、过期、禁用、设备绑定和会话签名的失败测试。
- [ ] 实现无数据库依赖的最小函数并通过测试。

### Task 2: SQLite 迁移与服务器授权/选手库 API

**Files:**
- Modify: `cloud-match-server/src/db.ts`
- Modify: `cloud-match-server/src/app.ts`
- Modify: `cloud-match-server/src/schemas.ts`
- Modify: `cloud-match-server/src/config.ts`
- Create: `cloud-match-server/src/v2-api.ts`
- Create: `cloud-match-server/tests/v2-api.test.ts`

- [ ] 增加授权卡、授权设备、选手实体及 ID 归属表和索引。
- [ ] 增加 `/api/v2/auth/activate`、`/api/v2/auth/validate`、`/api/v2/player-library` 读写接口。
- [ ] 所有写入在事务中完成，冲突返回明确的 409，不覆盖生产旧表。
- [ ] 通过 API 集成测试验证合成卡密、过期和 ID 冲突。

### Task 3: 管理后台与 OCR 禁用策略

**Files:**
- Modify: `cloud-match-server/src/admin.ts`
- Modify: `cloud-match-server/src/admin-data.ts`
- Modify: `cloud-match-server/src/admin-page.ts`
- Test: `cloud-match-server/tests/admin.test.ts`

- [ ] 增加授权卡查询/创建/禁用、主播 OCR 截止时间、选手库查看/修正 API。
- [ ] 管理页面增加授权、主播和选手库三个区域，沿用 Basic Auth + CSRF。
- [ ] 增加输入校验和审计式操作结果，不显示设备令牌。

### Task 4: 测试服隔离部署材料

**Files:**
- Create: `cloud-match-server/deploy/dnf-cloud-match-test.service`
- Create: `cloud-match-server/deploy/test-server.env`
- Create: `cloud-match-server/deploy/install-test.sh`
- Modify: `cloud-match-server/README-部署.md`
- Create: `cloud-match-server/deploy/endpoint-manifest.test.json`

- [ ] 固定测试端口 28880/28881、独立服务名、数据库、日志和管理员密码。
- [ ] 安装脚本只复制测试目录，不停止或修改 18880/18881 服务。
- [ ] 写明 OSS endpoint manifest 的最小 JSON 格式和回滚方式。

### Task 5: C++ 地址缓存与服务器授权桥接

**Files:**
- Modify: `LicenseLease.h/.cpp`
- Modify: `DNFGameCaptureDlg.h/.cpp`
- Modify: `CloudMatchClient.h/.cpp`
- Test: `scripts/check-cloud-endpoint-cache.ps1`

- [ ] 抽出“租约地址优先、连接失败后 OSS 回源一次”的可测试决策函数。
- [ ] 新客户端对测试 API 使用授权返回的 `cloudServerUrl`，旧响应继续使用旧云函数地址。
- [ ] 授权失败不刷新 endpoint；不把真实卡密写入源码或日志。
- [ ] 离线时保持本地功能，使用租约中的 `ocrDisabledUntil` 做本地截止判断。

### Task 6: 构建、静态检查和发布说明

- [ ] 运行服务端测试和 TypeScript 构建。
- [ ] 运行 `node --check`、`git diff --check`，再尝试 Release x64 构建。
- [ ] 生成不含密钥的测试服部署包和 SHA-256 清单。
- [ ] 在最终报告中明确哪些步骤已验证、哪些需要用户在服务器/OSS 控制台执行。
