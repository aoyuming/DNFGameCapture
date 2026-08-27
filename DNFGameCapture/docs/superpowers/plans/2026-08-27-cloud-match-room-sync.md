# 公网比赛房间同步 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建固定三房间的公网比赛快照服务，并让桌面客户端自动上传本机变化、比较所有主播数据、由用户手动确认同步。

**Architecture:** 新建独立 Node.js/TypeScript 模块化单体服务，Express 负责健康检查和设备注册，Socket.IO 负责房间连接、快照上传、对比和目标快照获取，SQLite/WAL 保存设备与每台设备的最新快照。桌面端新增隔离的 `CloudMatchClient` 网络线程，通过 WinHTTP WebSocket 实现 Engine.IO 4/Socket.IO 4 必要协议，所有比赛数据应用仍由 MFC 主线程复用现有 TeamSync 校验入口完成。

**Tech Stack:** Node.js 22、TypeScript、Express、Socket.IO、better-sqlite3、Zod、Vitest、Supertest、C++17、MFC、WinHTTP WebSocket、WebView2、SQLite。

---

## 文件结构

### 新建服务器目录 `cloud-match-server/`

- `package.json`：依赖、开发、测试、构建和启动脚本。
- `tsconfig.json`、`vitest.config.ts`：严格 TypeScript 与测试配置。
- `src/config.ts`：端口、数据库路径和限额。
- `src/db.ts`：SQLite 初始化、迁移、WAL 和三个固定房间种子。
- `src/schemas.ts`：设备、房间、比赛快照和 Socket 消息的 Zod schema。
- `src/identity.ts`：设备注册、令牌哈希和鉴权。
- `src/rooms.ts`：加入、改名、切房、退出及在线状态。
- `src/snapshots.ts`：revision 去重、最新快照事务写入和审计。
- `src/comparison.ts`：主号/小号身份匹配、红蓝视角归一化、相似度和差异。
- `src/socket.ts`：Socket.IO 鉴权、事件和房间广播。
- `src/app.ts`：Express/Socket.IO 组装，供测试复用。
- `src/server.ts`：生产启动入口。
- `tests/*.test.ts`：数据库、房间、快照、比较和 Socket 集成测试。

### 新建桌面端文件

- `CloudMatchProtocol.h/.cpp`：Engine.IO/Socket.IO 帧编码解码和云端 JSON schema 边界。
- `CloudMatchClient.h/.cpp`：唯一网络线程、WebSocket 生命周期、重连、命令队列和线程安全状态快照。
- `scripts/cloud_match_protocol_test.cpp`：不依赖 MFC 的协议单元测试。
- `scripts/check-cloud-match-feature.ps1`：静态回归检查。

### 修改现有文件

- `DNFGameCaptureDlg.h/.cpp`：配置、生命周期、400ms 快照合并、首次入房、手动应用和 Web 状态。
- `DNFGameCapture.vcxproj/.filters`：加入新 C++ 文件。
- `resource.h`、`DNFGameCapture.rc`：专业窗口只读云端状态。
- `web前端/index.html`：顶部房间标签、首次加入窗口和云端同步面板。
- `web前端/main.js`：状态渲染、房间命令、比较与手动同步交互。
- `web前端/style.css`：状态色、弹窗和紧凑对比表。

---

### Task 1: 初始化 TypeScript 服务和 SQLite 房间数据库

**Files:**
- Create: `cloud-match-server/package.json`
- Create: `cloud-match-server/tsconfig.json`
- Create: `cloud-match-server/vitest.config.ts`
- Create: `cloud-match-server/src/config.ts`
- Create: `cloud-match-server/src/db.ts`
- Create: `cloud-match-server/tests/db.test.ts`

- [ ] **Step 1: 写失败的数据库测试**

测试使用临时 SQLite 文件，断言启动迁移后固定存在稳定 ID：

```ts
expect(db.prepare('select id, display_name from rooms order by id').all()).toEqual([
  { id: '59', display_name: '59房' },
  { id: 'li-yong', display_name: '李永房' },
  { id: 'wen-rou', display_name: '温柔房' },
]);
expect(db.pragma('journal_mode', { simple: true })).toBe('wal');
expect(db.pragma('foreign_keys', { simple: true })).toBe(1);
```

- [ ] **Step 2: 安装依赖并确认测试失败**

Run: `cd cloud-match-server; npm install; npm test -- --run tests/db.test.ts`

Expected: FAIL，因为 `src/db.ts` 尚不存在。

- [ ] **Step 3: 实现最小数据库迁移**

`package.json` 固定使用以下依赖边界：

```json
{
  "name": "dnf-cloud-match-server",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "scripts": {
    "dev": "tsx watch src/server.ts",
    "build": "tsc -p tsconfig.json",
    "start": "node dist/server.js",
    "test": "vitest"
  },
  "dependencies": {
    "better-sqlite3": "^11.8.1",
    "express": "^4.21.2",
    "socket.io": "^4.8.1",
    "zod": "^3.24.2"
  },
  "devDependencies": {
    "@types/better-sqlite3": "^7.6.12",
    "@types/express": "^5.0.0",
    "@types/node": "^22.13.4",
    "socket.io-client": "^4.8.1",
    "supertest": "^7.0.0",
    "tsx": "^4.19.3",
    "typescript": "^5.7.3",
    "vitest": "^3.0.5"
  }
}
```

`tsconfig.json` 使用 `strict: true`、`module/moduleResolution: NodeNext`、`target: ES2022`，输出目录为 `dist`，源码目录为 `src`。

创建表：

```sql
CREATE TABLE devices (
  id TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL
);
CREATE TABLE rooms (
  id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL UNIQUE
);
CREATE TABLE memberships (
  device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
  room_id TEXT NOT NULL REFERENCES rooms(id),
  broadcaster_name TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE TABLE snapshots (
  device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
  room_id TEXT NOT NULL REFERENCES rooms(id),
  client_revision INTEGER NOT NULL,
  content_hash TEXT NOT NULL,
  change_source TEXT NOT NULL,
  synced_from_device_id TEXT,
  synced_from_revision INTEGER,
  payload_json TEXT NOT NULL,
  received_at INTEGER NOT NULL
);
CREATE TABLE snapshot_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id TEXT NOT NULL,
  room_id TEXT NOT NULL,
  client_revision INTEGER NOT NULL,
  accepted INTEGER NOT NULL,
  reason TEXT NOT NULL,
  received_at INTEGER NOT NULL
);
```

启用 `journal_mode=WAL`、`foreign_keys=ON`、`busy_timeout=5000`，并用 `INSERT OR IGNORE` 写入三个房间。

- [ ] **Step 4: 运行数据库测试**

Run: `cd cloud-match-server; npm test -- --run tests/db.test.ts`

Expected: PASS。

- [ ] **Step 5: 提交服务器骨架**

```powershell
git add cloud-match-server/package.json cloud-match-server/package-lock.json cloud-match-server/tsconfig.json cloud-match-server/vitest.config.ts cloud-match-server/src/config.ts cloud-match-server/src/db.ts cloud-match-server/tests/db.test.ts
git commit -m "新增云端比赛房间服务骨架"
```

### Task 2: 实现设备身份和固定房间成员关系

**Files:**
- Create: `cloud-match-server/src/schemas.ts`
- Create: `cloud-match-server/src/identity.ts`
- Create: `cloud-match-server/src/rooms.ts`
- Create: `cloud-match-server/src/app.ts`
- Create: `cloud-match-server/src/server.ts`
- Create: `cloud-match-server/tests/identity-rooms.test.ts`

- [ ] **Step 1: 写设备注册和入房失败测试**

覆盖以下行为：

```ts
const registered = await request(app).post('/api/devices/register').send({ deviceId });
expect(registered.body.deviceToken).toMatch(/^[A-Za-z0-9_-]{40,}$/);

const duplicate = await request(app).post('/api/devices/register').send({ deviceId });
expect(duplicate.status).toBe(409);
expect(duplicate.body).toEqual({ ok: false, code: 'device_already_registered' });

const joined = await emitAck(socket, 'room:join', {
  roomId: 'li-yong', broadcasterName: '主播甲'
});
expect(joined).toMatchObject({ ok: true, room: { displayName: '李永房' } });

const rejected = await emitAck(socket, 'room:join', {
  roomId: 'unknown', broadcasterName: '主播甲'
});
expect(rejected).toMatchObject({ ok: false, code: 'room_not_found' });
```

同时断言：重复设备 ID 返回 409 且不返回令牌、不改变原令牌；空白名称、超过 32 个 grapheme cluster 的名称、控制/格式字符名称、错误令牌被拒绝；同名主播允许加入并返回设备 ID 后四位。

- [ ] **Step 2: 运行身份测试确认失败**

Run: `cd cloud-match-server; npm test -- --run tests/identity-rooms.test.ts`

Expected: FAIL，注册路由和 Socket 处理尚不存在。

- [ ] **Step 3: 实现令牌与成员服务**

- 设备令牌使用 `crypto.randomBytes(32).toString('base64url')`。
- SQLite 只保存 `sha256(deviceToken)`，比较使用 `timingSafeEqual`。
- 设备注册仅插入新设备；已存在 `deviceId` 返回 HTTP 409 `device_already_registered`，不旋转、不覆盖原令牌，也不提供未鉴权的令牌重置接口。客户端丢失令牌时生成新的随机设备 ID。
- Socket handshake auth 固定为 `{ deviceId, deviceToken, protocolVersion: 1 }`。
- 事件固定为 `room:list`、`room:join`、`room:rename`、`room:leave`、`room:status`。
- `room:leave` 删除 membership 和该设备快照，但保留 device 身份。
- 新连接替换同设备旧 Socket，避免一台设备产生两个在线票。

- [ ] **Step 4: 运行身份与入房测试**

Run: `cd cloud-match-server; npm test -- --run tests/identity-rooms.test.ts`

Expected: PASS。

- [ ] **Step 5: 提交身份和房间功能**

```powershell
git add cloud-match-server/src cloud-match-server/tests/identity-rooms.test.ts
git commit -m "实现云端设备身份和固定房间"
```

### Task 3: 实现快照校验、保存和所有主播对比

**Files:**
- Modify: `cloud-match-server/src/schemas.ts`
- Create: `cloud-match-server/src/snapshots.ts`
- Create: `cloud-match-server/src/comparison.ts`
- Modify: `cloud-match-server/src/app.ts`
- Create: `cloud-match-server/tests/snapshots.test.ts`
- Create: `cloud-match-server/tests/comparison.test.ts`

- [ ] **Step 1: 写快照边界失败测试**

构造 `schemaVersion: 1`、两队各 4 人的合法快照，断言：

- revision 递增时覆盖最新快照。
- 相同或更旧 revision 返回 `stale_revision`。
- 完全相同内容返回 `duplicate_snapshot`。
- JSON 超过 64KB、不是 8 人、负数战绩、超长主号和未知 `changeSource` 被拒绝。
- 服务端使用自己的 `receivedAt`，不信任客户端时间判断在线和过期。

- [ ] **Step 2: 写视角归一化与相似度失败测试**

创建三台设备：A 正常红蓝、B 红蓝完全互换但身份战绩相同、C 漏掉一次击杀。断言 A/B 被归为同组且相似度 100%，C 的差异明确指出对应主号的击杀差 1。

再创建 D，它的 `changeSource=cloud_sync` 且 `syncedFrom=A/revision`，断言 D 不增加 A 数据的多数派票数。

- [ ] **Step 3: 运行快照和比较测试确认失败**

Run: `cd cloud-match-server; npm test -- --run tests/snapshots.test.ts tests/comparison.test.ts`

Expected: FAIL。

- [ ] **Step 4: 实现快照 schema 与事务写入**

定义核心类型：

```ts
type ChangeSource = 'ocr' | 'manual' | 'local_restore' | 'cloud_sync';
type Player = { mainName: string; aliases: string[]; kills: number; deaths: number; ak: number; streak: number };
type MatchSnapshot = {
  schemaVersion: 1;
  clientRevision: number;
  clientTime: number;
  changeSource: ChangeSource;
  syncedFrom?: { deviceId: string; revision: number };
  redScore: number;
  blueScore: number;
  redPlayers: [Player, Player, Player, Player];
  bluePlayers: [Player, Player, Player, Player];
  redPickFirst: boolean;
  teamsFlipped: boolean;
  outputSeatLabel: boolean;
  lastKillTeam: 'red' | 'blue' | '';
};
```

快照保存、revision 检查、hash 去重和审计写入必须在同一 SQLite 事务中完成。

- [ ] **Step 5: 实现比较算法**

- 名称规范化使用 Unicode trim + ASCII 大小写折叠，不做模糊字符串包含。
- 玩家身份集合为主号和全部非空小号；两人集合有交集即视为同一身份。
- 分别计算原方向与红蓝互换方向的 8 人最大一一匹配，取身份匹配数更多者；相同则以总战绩差更小者为准。
- 权重固定为名单 40、战绩 35、比分 15、状态 10，结果四舍五入到整数百分比。
- 30 秒标记 stale，120 秒后排除多数派与相似分组。
- `cloud_sync + syncedFrom` 形成来源链，比较投票按链根设备去重。

- [ ] **Step 6: 运行全部服务器测试**

Run: `cd cloud-match-server; npm test -- --run`

Expected: PASS。

- [ ] **Step 7: 提交快照与比较功能**

```powershell
git add cloud-match-server/src cloud-match-server/tests
git commit -m "实现比赛快照保存和主播数据对比"
```

### Task 4: 实现 Socket.IO 房间事件和八客户端集成测试

**Files:**
- Create: `cloud-match-server/src/socket.ts`
- Modify: `cloud-match-server/src/app.ts`
- Create: `cloud-match-server/tests/socket-integration.test.ts`

- [ ] **Step 1: 写八客户端集成失败测试**

启动随机端口服务器，注册并连接 8 个设备加入同一房间。测试：

- `snapshot:upload` 只在当前房间保存。
- `room:comparison` 返回 8 个成员摘要，但不返回设备令牌。
- `snapshot:get` 只能读取同房间目标。
- 断开后在线状态在 5 秒内变为离线，快照仍可读取。
- 改房间后旧房间不再看到该设备。
- 400 次快速合法上传不会阻塞事件循环，最终 revision 为最大值。

- [ ] **Step 2: 运行集成测试确认失败**

Run: `cd cloud-match-server; npm test -- --run tests/socket-integration.test.ts`

Expected: FAIL。

- [ ] **Step 3: 实现 Socket 事件**

固定事件和 ACK：

```ts
'snapshot:upload'    // { snapshot } -> { ok, acceptedRevision, reason? }
'room:comparison'    // {} -> { ok, generatedAt, members, groups }
'snapshot:get'       // { targetDeviceId } -> { ok, snapshot, broadcasterName, receivedAt }
'room:changed'       // 服务端通知客户端重新拉取比较摘要
```

每个事件先鉴权并读取当前 membership。`room:changed` 只携带 room revision，不主动携带或应用其他主播快照。

- [ ] **Step 4: 运行所有 Node 测试和 TypeScript 构建**

Run: `cd cloud-match-server; npm test -- --run; npm run build`

Expected: 全部 PASS，`tsc` 无错误。

- [ ] **Step 5: 提交实时服务**

```powershell
git add cloud-match-server/src cloud-match-server/tests
git commit -m "完成云端房间实时快照接口"
```

### Task 5: 实现 C++ Socket.IO 协议和隔离网络客户端

**Files:**
- Create: `CloudMatchProtocol.h`
- Create: `CloudMatchProtocol.cpp`
- Create: `CloudMatchClient.h`
- Create: `CloudMatchClient.cpp`
- Create: `scripts/cloud_match_protocol_test.cpp`
- Create: `scripts/check-cloud-match-protocol-test.ps1`
- Modify: `DNFGameCapture.vcxproj`
- Modify: `DNFGameCapture.vcxproj.filters`

- [ ] **Step 1: 写协议解析失败测试**

测试至少覆盖：

```cpp
assert(ParseEngineIoPacket("0{\"sid\":\"abc\",\"pingInterval\":25000,\"pingTimeout\":20000}").sid == "abc");
assert(EncodeSocketEvent("room:list", json::object()) == "42[\"room:list\",{}]");
assert(ParseSocketAck("431[{\"ok\":true}]").ackId == 1);
assert(IsPingPacket("2"));
assert(BuildPongPacket("2") == "3");
assert(!ValidateCloudPayload(std::string(65537, 'x')));
```

- [ ] **Step 2: 运行测试确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-protocol-test.ps1`

Expected: FAIL，协议文件不存在。

- [ ] **Step 3: 实现协议纯函数**

只实现本功能需要的 Engine.IO 4 / Socket.IO 4 子集：open `0`、ping `2`、pong `3`、namespace connect `40`、event `42`、ack `43`、disconnect `41`。所有 payload 上限 64KB，JSON 解析失败返回结构化错误而不是抛到网络线程外。

- [ ] **Step 4: 实现 `CloudMatchClient`**

公共接口固定为：

```cpp
struct CloudMatchStatusSnapshot {
    bool configured, connecting, connected, reconnecting;
    std::string roomId, roomName, broadcasterName, statusText;
    std::uint64_t roomRevision;
};

class CloudMatchClient {
public:
    void Configure(const std::wstring& serverUrl, const std::string& deviceId,
                   const std::string& deviceToken);
    void Start();
    void Stop();
    void JoinRoom(const std::string& roomId, const std::string& broadcasterName);
    void Rename(const std::string& broadcasterName);
    void LeaveRoom();
    void UploadSnapshot(std::string snapshotJson);
    void RequestComparison(unsigned int requestId);
    void RequestSnapshot(unsigned int requestId, const std::string& targetDeviceId);
    CloudMatchStatusSnapshot GetStatusSnapshot() const;
    void SetMessageCallback(std::function<void(std::string)> callback);
    std::size_t DispatchMessages(std::size_t maxCount = 32);
};
```

实现要求：

- 唯一 `std::thread` 创建、配置并正常关闭 WinHTTP session/connect/request/WebSocket handle；`Stop` 和 `Configure` 通过同一原子交换关闭当前阻塞的 request/WebSocket 以取消 I/O，worker 使用 compare-exchange 清理避免重复关闭。`Configure` 先递增 generation，再清空旧命令、最新上传、ACK 和目标房间并唤醒 worker；handle 发布、每次发送和阻塞调用返回后都检查 generation，旧配置结果不得进入新配置。
- UI 线程只向有界命令队列写命令，不等待网络 I/O。
- worker 只把归一化消息写入最多 128 条的有界入站队列，绝不调用用户回调。宿主 UI 线程调用 `DispatchMessages(maxCount)` 排空消息；它在锁内复制回调和取出消息、释放容量并唤醒 worker，在锁外调用，因此回调可安全调用 `Stop`，worker 也绝不等待 UI 排空。
- 注册、入房、改名、离房、上传、对比和快照读取在公共方法接受前必须预留受保护结果槽；已排队受保护结果与预留槽合计最多 96，命令转为 ACK 时转移预留，结果入队时消费预留，受保护结果永不因溢出被删除。无槽时公共方法立即返回 `false/queue_full`；其余 128 总容量供 `room_changed`、同设备 `room_presence` 和云端瞬态通知使用，可合并或删除最旧通知。`DispatchMessages` 释放结果容量后，新的请求可立即恢复接受。
- `DesiredRoom` 保存 generation；`JoinRoom` 必须在同一互斥区内捕获当前 generation、预留结果槽、写命令和更新目标房间，`Configure` 也在同一互斥区推进 generation 并清空目标房间。自动重加入只允许 `desired.generation == activeConfig.generation`，旧 Join 不得在重新配置后恢复旧房间。
- WebSocket URL 使用 `/socket.io/?EIO=4&transport=websocket`。
- 收到 ping 立即回 pong；掉线后按 1、2、5、10、20 秒退避重连。
- `UploadSnapshot` 队列只保留最新一份，避免离线积压。上传上限按完整 UTF-8 Socket.IO 事件计算，包含事件名、`{"snapshot":...}` 包装和 ACK 数字；事件上限取本地 65536 与 Engine.IO `maxPayload` 的较小值。超限或非法编码只生成一次 `snapshot_upload_result`（`payload_too_large`/`invalid_payload`），不得重排、重连或重试；服务端 schema 仍保留 64KB 概念，但传输包装可进一步缩小原始快照额度。
- 所有状态和消息通过快照/UI 排空发布，析构时取消 WinHTTP 并 join，不使用 detach。`Config` 的特殊成员、重新配置、`Stop` 和析构必须用 `SecureZeroMemory` 或 volatile 写保证擦除设备令牌，并擦除认证连接串、REST 请求/响应、过期注册结果、已派发注册消息及被清空队列；任何日志不得包含令牌。`Command` 析构时调用安全擦除，禁止复制并提供 `noexcept` 移动构造/赋值，移动赋值先擦除目标，确保设备注册数据、请求负载和快照类字符串不会因队列或局部临时对象残留。

- [ ] **Step 5: 运行协议测试和静态线程检查**

Run: `powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-protocol-test.ps1`

Expected: PASS，并确认源码不存在 `.detach()`、worker 回调、UI 同步 `WinHttpReceiveResponse` 或无上限消息队列；覆盖 UI 线程 `DispatchMessages`/回调内 `Stop`、96 个受保护结果与预留混合填满后拒绝且排空恢复、旧 Join/Configure generation 竞态、完整事件精确边界、`Command` 移动专有析构和安全擦除静态断言。

- [ ] **Step 6: 提交 C++ 云端客户端**

```powershell
git add CloudMatchProtocol.* CloudMatchClient.* scripts/cloud_match_protocol_test.cpp scripts/check-cloud-match-protocol-test.ps1 DNFGameCapture.vcxproj DNFGameCapture.vcxproj.filters
git commit -m "新增公网比赛房间客户端"
```

### Task 6: 接入比赛快照、配置和首次入房流程

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Create: `scripts/check-cloud-match-feature.ps1`

- [ ] **Step 1: 写首次入房和上传静态失败检查**

脚本断言存在：`[CloudMatch]`、三个固定房间、`CloudMatchClient` 成员、`400ms` 合并计时、`cmd_cloud_room_join`、`cmd_cloud_room_skip_once`、`cmd_cloud_room_rename`、`cmd_cloud_room_leave`，并拒绝任何 Socket 回调直接调用 `ApplyTeamSyncSnapshot`。

- [ ] **Step 2: 运行静态检查确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1`

Expected: FAIL。

- [ ] **Step 3: 接入配置与生命周期**

`config.ini` 新增：

```ini
[CloudMatch]
ServerUrl=http://127.0.0.1:18880
DeviceId=
DeviceToken=
RoomId=
BroadcasterName=
ClientRevision=0
```

规则：没有 `RoomId` 时 Web 页面就绪后弹首次选择；选择“不加入房间”只设置内存变量 `skipPromptThisRun`，不写配置；成功加入后才保存房间和名称。启动时有已保存身份便后台连接，不弹窗。

- [ ] **Step 4: 复用比赛快照构建与应用入口**

将现有 `BuildTeamSyncSnapshotPayloadUnlocked()` 产物包装为云端 `schemaVersion:1` 快照，增加 `clientRevision/clientTime/changeSource/syncedFrom`。所有会改变比赛内容的路径最终调用统一的 `OnMatchStateChanged(ChangeSource source)`：

- 更新 LAN 快照。
- 使云端撤销备份按规则失效。
- 在 400ms 后把最新完整快照交给 `CloudMatchClient`。

云端收到的消息通过 `WM_CLOUD_MATCH_MESSAGE` 投递到主线程；网络线程不得访问选手数组、WebView 或 MFC 控件。

- [ ] **Step 5: 实现首次选择 Web 弹窗**

弹窗固定为四个单选项和主播名称输入。加入按钮仅在房间已选且名称 trim 后为 1–32 字符时启用；“不加入房间”不要求名称。加入中禁用按钮，失败保留输入并显示服务端错误。

- [ ] **Step 6: 运行前端与静态检查**

Run: `node --check web前端/main.js; powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1`

Expected: PASS。

- [ ] **Step 7: 提交首次入房和上传功能**

```powershell
git add DNFGameCaptureDlg.* web前端/index.html web前端/main.js web前端/style.css scripts/check-cloud-match-feature.ps1
git commit -m "接入云端房间身份和比赛快照上传"
```

### Task 7: 实现主播对比、差异预览、手动同步和撤销

**Files:**
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Modify: `scripts/check-cloud-match-feature.ps1`

- [ ] **Step 1: 扩展失败检查和前端 DOM 测试约束**

断言命令存在：

```text
cmd_cloud_room_compare
cmd_cloud_snapshot_preview
cmd_cloud_snapshot_apply
cmd_cloud_snapshot_undo
```

并断言应用函数调用 `ValidateTeamSyncSnapshot`、创建一次备份、设置 `changeSource=cloud_sync` 和 `syncedFrom`，但对比响应绝不自动调用应用函数。

- [ ] **Step 2: 运行检查确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1`

Expected: FAIL。

- [ ] **Step 3: 实现“更多 > 云端比赛同步”面板**

面板包含：

- 房间/主播/连接状态。
- 修改名称、切房和退出。
- 成员列表：在线、更新时间、比分、相似度、差异数、来源。
- 相似组以相同颜色边线标记，不做夸张卡片嵌套。
- 点击成员打开差异表；明确显示视角是否已按红蓝互换归一化。
- 只有取得完整目标快照且仍属于当前房间时启用“同步到本机”。

- [ ] **Step 4: 实现主线程手动应用和一次撤销**

应用前重新验证 64KB、schema、8 人和数值范围，并保存本机快照、事件分界及当前 client revision。成功后调用现有 `RefreshAfterTeamSyncApply()`、保存配置和更新 TXT。

新的云端快照设置：

```json
{
  "changeSource": "cloud_sync",
  "syncedFrom": { "deviceId": "目标设备", "revision": 123 }
}
```

撤销只在没有后续本地修改时可用；撤销本身标记 `local_restore` 并上传恢复后的当前状态。

- [ ] **Step 5: 运行检查**

Run: `node --check web前端/main.js; powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1; git diff --check`

Expected: PASS。

- [ ] **Step 6: 提交比较和手动同步**

```powershell
git add DNFGameCaptureDlg.* web前端/index.html web前端/main.js web前端/style.css scripts/check-cloud-match-feature.ps1
git commit -m "新增主播数据对比和手动云端同步"
```

### Task 8: 增加主 Web 与专业窗口房间状态

**Files:**
- Modify: `resource.h`
- Modify: `DNFGameCapture.rc`
- Modify: `DNFGameCaptureDlg.h`
- Modify: `DNFGameCaptureDlg.cpp`
- Modify: `web前端/index.html`
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`
- Modify: `scripts/check-cloud-match-feature.ps1`

- [ ] **Step 1: 写状态显示失败检查**

检查四种状态文案和 class：`online/reconnecting/offline/not-joined`，检查顶部状态标签点击后打开云端同步面板，并检查 C++ 专业窗口有独立 `IDC_STATIC_CLOUD_ROOM_STATUS`。同时断言源码没有调用 `SetWindowText` 修改主窗口或展示窗口标题。

- [ ] **Step 2: 运行检查确认失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1`

Expected: FAIL。

- [ ] **Step 3: 实现主 Web 状态标签**

显示规则固定为：

- 绿色：`李永房 · 主播甲`。
- 黄色：`李永房 · 重连中`。
- 红色：`李永房 · 离线`。
- 灰色：`未加入云端房间`。

标签位于主 Web 顶部状态区，使用单行省略保护窄窗口，但 tooltip 显示完整文字；点击关闭“更多”菜单并打开云端同步面板。

- [ ] **Step 4: 实现专业窗口只读状态**

在现有 434×200 对话框底部状态区域新增 `CStatic`，由主线程根据 `CloudMatchStatusSnapshot` 更新同样语义的短文本。保持所有窗口标题原值，不改变直播伴侣窗口匹配。

- [ ] **Step 5: 运行前端和静态检查**

Run: `node --check web前端/main.js; powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1`

Expected: PASS。

- [ ] **Step 6: 提交状态显示**

```powershell
git add resource.h DNFGameCapture.rc DNFGameCaptureDlg.* web前端/index.html web前端/main.js web前端/style.css scripts/check-cloud-match-feature.ps1
git commit -m "显示当前云端比赛房间状态"
```

### Task 9: 端到端验证、Release 构建和前端同步

**Files:**
- Modify: only the specific Task 1–8 source or test file responsible for a failing verification
- Copy after verification: `x64/Release/web前端/index.html`
- Copy after verification: `x64/Release/web前端/main.js`
- Copy after verification: `x64/Release/web前端/style.css`

- [ ] **Step 1: 运行服务器完整测试和构建**

Run: `cd cloud-match-server; npm test -- --run; npm run build`

Expected: 所有测试 PASS，TypeScript 构建成功。

- [ ] **Step 2: 运行 C++/Web 静态回归**

Run:

```powershell
node --check web前端/main.js
powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-protocol-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/check-cloud-match-feature.ps1
powershell -ExecutionPolicy Bypass -File scripts/check-key-mapping-lan-feature.ps1
powershell -ExecutionPolicy Bypass -File scripts/check-key-mapping-lan-protocol-test.ps1
powershell -ExecutionPolicy Bypass -File scripts/check-capture-source-switch-feature.ps1
git diff --check
```

Expected: 全部 PASS。

- [ ] **Step 3: 启动本地服务器做真实八客户端冒烟测试**

Run: `cd cloud-match-server; npm run dev`

验证：首次不加入后重启仍询问；加入三个房间；同房间 8 个模拟客户端上传；掉线客户端保持最后快照；比较、视角归一化、差异预览、手动应用和撤销均符合规格；服务停止时本地 OCR/TXT 不受影响。

- [ ] **Step 4: 构建 Release x64**

Run:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" "..\DNFGameCapture.slnx" /p:Configuration=Release /p:Platform=x64 /m
```

Expected: `Build succeeded`，产物为 `C:\Users\BRO\source\repos\DNFGameCapture\x64\Release\DNFGameCapture.exe`。

- [ ] **Step 5: 同步前端并校验 SHA-256**

复制 `index.html/main.js/style.css` 到 `C:\Users\BRO\source\repos\DNFGameCapture\x64\Release\web前端`，随后对源文件和 Release 文件分别运行 `Get-FileHash -Algorithm SHA256`，三组哈希必须一致。

- [ ] **Step 6: 最终提交**

```powershell
git add cloud-match-server CloudMatchClient.* CloudMatchProtocol.* DNFGameCapture.vcxproj DNFGameCapture.vcxproj.filters DNFGameCaptureDlg.* DNFGameCapture.rc resource.h scripts web前端
git commit -m "完成公网比赛房间数据对比和手动同步"
```

最终提交前再次运行 `git status --short`，只提交本计划涉及的源码、测试和服务器文件，不加入 `.vs`、`x64`、日志、OBJ 或其他用户未跟踪文件。
