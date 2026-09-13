# 服务器端主播归属关联实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 仅修改服务器，实现投稿、密钥和主播之间可审核的关联，并在管理后台显示主播来源、IP、地区和对应完整密钥。

**Architecture:** 新建独立的归属服务，持久化授权 HTTP IP、主播 Socket IP 和人工/自动关联。自动关联只接受同一公网 IP、短时间内一对一的唯一候选；人工绑定永久优先。现有三个管理页面只消费安全的关联摘要，完整密钥仍通过现有单条 reveal 接口读取。

**Tech Stack:** TypeScript、Express、Socket.IO、better-sqlite3、Vitest、原生管理页 JavaScript、geoip-lite、PowerShell 发布脚本。

---

### Task 1: IP 地区与归属核心

**Files:**
- Create: `cloud-match-server/src/ip-region.ts`
- Create: `cloud-match-server/src/broadcaster-attribution.ts`
- Create: `cloud-match-server/tests/broadcaster-attribution.test.ts`
- Modify: `cloud-match-server/package.json`
- Modify: `cloud-match-server/package-lock.json`

- [x] **Step 1: 安装本地 GeoIP 依赖**

Run: `npm install geoip-lite && npm install --save-dev @types/geoip-lite`

Expected: `package.json` 和锁文件只增加 GeoIP 运行时依赖及类型依赖。

- [x] **Step 2: 先写失败测试**

```ts
test('只在同一公网 IP 的授权与在线主播均唯一时自动关联', () => {
  const service = createBroadcasterAttributionService(db, {
    resolveRegion: () => '中国 · 浙江 · 杭州',
  });
  service.observeLicense({ licenseId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 100 });
  service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 101 });
  expect(service.getBroadcasterAttribution('socket-a')?.licenseId).toBe(licenseId);
});

test('共享 IP 存在多个候选时不自动关联且人工关联不会被覆盖', () => {
  service.observeLicense({ licenseId: firstId, licenseDeviceId: 'machine-a', ipAddress: '47.1.2.3', observedAt: 100 });
  service.observeLicense({ licenseId: secondId, licenseDeviceId: 'machine-b', ipAddress: '47.1.2.3', observedAt: 100 });
  service.connectBroadcaster({ deviceId: 'socket-a', broadcasterName: '主播甲', ipAddress: '47.1.2.3', observedAt: 101 });
  expect(service.getBroadcasterAttribution('socket-a')).toBeNull();
  service.manualLink('socket-a', firstId, 102);
  service.observeLicense({ licenseId: secondId, licenseDeviceId: 'machine-b', ipAddress: '47.1.2.3', observedAt: 103 });
  expect(service.getBroadcasterAttribution('socket-a')).toMatchObject({ licenseId: firstId, source: 'manual' });
});
```

- [x] **Step 3: 运行测试并确认因模块不存在而失败**

Run: `npm test -- --run tests/broadcaster-attribution.test.ts`

Expected: FAIL，原因是 `broadcaster-attribution.ts` 尚不存在。

- [x] **Step 4: 实现最小核心服务**

```ts
export interface BroadcasterAttributionService {
  observeLicense(input: LicenseObservation): void;
  connectBroadcaster(input: BroadcasterObservation): void;
  renameBroadcaster(deviceId: string, broadcasterName: string, observedAt: number): void;
  disconnectBroadcaster(deviceId: string): void;
  manualLink(deviceId: string, licenseId: number, nowSec: number): BroadcasterLicenseLink;
  getActiveDeviceIps(): ReadonlyMap<string, string>;
  getBroadcasterAttribution(deviceId: string): BroadcasterLicenseLink | null;
}
```

实现启动幂等建表、IP 规范化、公网判断、120 秒唯一候选匹配、人工优先和本地地区解析。所有观察写入异常在服务边界内降级，不影响业务请求；人工写入异常正常返回。

- [x] **Step 5: 运行核心测试并提交**

Run: `npm test -- --run tests/broadcaster-attribution.test.ts`

Expected: PASS。

```bash
git add DNFGameCapture/cloud-match-server/package.json DNFGameCapture/cloud-match-server/package-lock.json DNFGameCapture/cloud-match-server/src/ip-region.ts DNFGameCapture/cloud-match-server/src/broadcaster-attribution.ts DNFGameCapture/cloud-match-server/tests/broadcaster-attribution.test.ts
git commit -m "实现服务器端主播归属核心"
```

### Task 2: 接入授权 HTTP 与主播 Socket

**Files:**
- Modify: `cloud-match-server/src/app.ts`
- Modify: `cloud-match-server/src/v2-api.ts`
- Modify: `cloud-match-server/src/socket.ts`
- Modify: `cloud-match-server/tests/v2-api.test.ts`
- Modify: `cloud-match-server/tests/socket-integration.test.ts`

- [ ] **Step 1: 先写 HTTP 与 Socket 失败测试**

```ts
test('现有 v2 请求记录授权机器码和解析后的客户端 IP', async () => {
  await request(app.expressApp).post('/api/v2/auth/activate').send({ key, deviceId: 'machine-a' }).expect(200);
  expect(app.db.prepare('SELECT ip_address FROM license_ip_observations WHERE license_device_id=?').get('machine-a'))
    .toEqual({ ip_address: '47.1.2.3' });
});

test('主播连接记录当前 IP 且断开后移出当前 IP 映射', async () => {
  const client = await connectAuthenticatedClient(app, 'socket-a');
  expect((await request(app.adminExpressApp).get('/admin/api/broadcasters/state').auth('admin', password)).body
    .broadcasters.find(item => item.deviceId === 'socket-a').currentIp).toBe('47.1.2.3');
  client.disconnect();
});
```

- [ ] **Step 2: 运行测试并确认缺少观察调用而失败**

Run: `npm test -- --run tests/v2-api.test.ts tests/socket-integration.test.ts`

Expected: FAIL，观察记录或 `getActiveDeviceIps` 不存在。

- [ ] **Step 3: 注入并调用归属服务**

```ts
const attribution = createBroadcasterAttributionService(db);
expressApp.use('/api/v2', createV2Api({
  db,
  now,
  resolveClientIp: remote => resolveClientIp('http', remote),
  attribution,
}));
```

授权激活、验证及 `requireSession` 成功后调用 `observeLicense`；Socket 连接、恢复会员身份、加入、改名和断开分别调用对应观察方法。旧协议字段和事件响应保持不变。

- [ ] **Step 4: 运行相关测试并提交**

Run: `npm test -- --run tests/v2-api.test.ts tests/socket-integration.test.ts`

Expected: PASS。

```bash
git add DNFGameCapture/cloud-match-server/src/app.ts DNFGameCapture/cloud-match-server/src/v2-api.ts DNFGameCapture/cloud-match-server/src/socket.ts DNFGameCapture/cloud-match-server/tests/v2-api.test.ts DNFGameCapture/cloud-match-server/tests/socket-integration.test.ts
git commit -m "接入授权与主播网络观察"
```

### Task 3: 管理 API 关联数据

**Files:**
- Modify: `cloud-match-server/src/admin-data.ts`
- Modify: `cloud-match-server/src/admin.ts`
- Modify: `cloud-match-server/src/license-store.ts`
- Modify: `cloud-match-server/src/library-admin-data.ts`
- Modify: `cloud-match-server/src/library-admin.ts`
- Modify: `cloud-match-server/tests/admin.test.ts`
- Modify: `cloud-match-server/tests/license-management.test.ts`
- Modify: `cloud-match-server/tests/library-admin.test.ts`

- [ ] **Step 1: 先写三个管理接口的失败测试**

```ts
expect(broadcaster).toMatchObject({
  currentIp: '47.1.2.3',
  region: '中国 · 浙江 · 杭州',
  license: { id: licenseId, broadcasterName: '主播甲', hasKey: true },
});
expect(licenseDevice.name).toContain('主播甲');
expect(submission.sourceBroadcasterName).toBe('主播甲');
expect(JSON.stringify(state)).not.toContain(rawLicenseKey);
```

- [ ] **Step 2: 运行测试并确认字段缺失**

Run: `npm test -- --run tests/admin.test.ts tests/license-management.test.ts tests/library-admin.test.ts`

Expected: FAIL，主播、密钥和投稿响应缺少归属字段。

- [ ] **Step 3: 实现安全摘要与人工绑定接口**

```ts
app.put('/admin/api/broadcasters/:deviceId/license', (request, response) => {
  const parsed = manualLicenseLinkSchema.safeParse(request.body);
  if (!deviceId || !parsed.success) return response.status(400).json({ ok: false, code: 'invalid_request' });
  response.json({ ok: true, link: attribution.manualLink(deviceId, parsed.data.licenseId, now()) });
});
```

`buildAdminState` 增加当前/最后 IP、地区和无明文密钥摘要；`listLicenseDevices` 增加关联主播名；`listAdminSubmissions` 增加来源主播名与设备 ID，筛选同时匹配主播名。

- [ ] **Step 4: 运行相关测试并提交**

Run: `npm test -- --run tests/admin.test.ts tests/license-management.test.ts tests/library-admin.test.ts`

Expected: PASS。

```bash
git add DNFGameCapture/cloud-match-server/src/admin-data.ts DNFGameCapture/cloud-match-server/src/admin.ts DNFGameCapture/cloud-match-server/src/license-store.ts DNFGameCapture/cloud-match-server/src/library-admin-data.ts DNFGameCapture/cloud-match-server/src/library-admin.ts DNFGameCapture/cloud-match-server/tests/admin.test.ts DNFGameCapture/cloud-match-server/tests/license-management.test.ts DNFGameCapture/cloud-match-server/tests/library-admin.test.ts
git commit -m "向管理接口提供主播归属信息"
```

### Task 4: 三个管理页面

**Files:**
- Modify: `cloud-match-server/src/broadcaster-admin-page.ts`
- Modify: `cloud-match-server/src/license-admin-page.ts`
- Modify: `cloud-match-server/src/library-admin-page.ts`
- Modify: `cloud-match-server/tests/license-admin-page.test.ts`
- Modify: `cloud-match-server/tests/admin.test.ts`
- Modify: `scripts/check-cloud-admin-hub.cjs`
- Modify: `scripts/check-license-admin.cjs`
- Modify: `scripts/check-cloud-library-admin.cjs`

- [ ] **Step 1: 先写页面契约失败测试**

```ts
expect(BROADCASTER_ADMIN_JS).toContain('/license');
expect(BROADCASTER_ADMIN_JS).toContain('/reveal');
expect(LICENSE_ADMIN_JS).toContain('broadcasterName');
expect(LIBRARY_ADMIN_JS).toContain('sourceBroadcasterName');
```

浏览器检查还要断言选中主播后显示完整卡密，未关联时显示“未关联密钥”，投稿列表显示主播名，所有表格在桌面和手机宽度无横向内容溢出。

- [ ] **Step 2: 运行测试并确认页面尚未渲染新字段**

Run: `npm test -- --run tests/admin.test.ts tests/license-admin-page.test.ts`

Expected: FAIL，缺少归属渲染和直接查看密钥逻辑。

- [ ] **Step 3: 按现有后台样式实现页面**

主播详情增加网络与授权信息区、已激活密钥选择器；选择主播后自动 POST `/admin/api/licenses/:id/reveal` 并直接显示结果。密钥页绑定栏显示主播名；选手库列表和详情显示来源主播，设备 ID 作为次要文字。所有动态文本继续使用 `textContent`。

- [ ] **Step 4: 运行页面测试和浏览器检查并提交**

Run:

```bash
npm test -- --run tests/admin.test.ts tests/license-admin-page.test.ts
node ../scripts/check-cloud-admin-hub.cjs
node ../scripts/check-license-admin.cjs
node ../scripts/check-cloud-library-admin.cjs
```

Expected: 全部 PASS，截图检查无重叠或溢出。

```bash
git add DNFGameCapture/cloud-match-server/src/broadcaster-admin-page.ts DNFGameCapture/cloud-match-server/src/license-admin-page.ts DNFGameCapture/cloud-match-server/src/library-admin-page.ts DNFGameCapture/cloud-match-server/tests/license-admin-page.test.ts DNFGameCapture/cloud-match-server/tests/admin.test.ts DNFGameCapture/scripts/check-cloud-admin-hub.cjs DNFGameCapture/scripts/check-license-admin.cjs DNFGameCapture/scripts/check-cloud-library-admin.cjs
git commit -m "在后台显示主播来源网络与密钥"
```

### Task 5: 完整验证与服务器 5.2.3 发布

**Files:**
- Create: `scripts/package-production-5.2.3.ps1`
- Create: `cloud-match-server/README-production-5.2.3.md`
- Modify: `cloud-match-server/tests/production-package.test.ts`
- Modify: `cloud-match-server/tests/production-deployment.test.ts`
- Modify: `docs/superpowers/plans/2026-09-13-server-side-broadcaster-attribution.md`

- [ ] **Step 1: 先更新发布测试并确认 5.2.3 产物缺失**

Run: `npm test -- --run tests/production-package.test.ts tests/production-deployment.test.ts`

Expected: FAIL，5.2.3 脚本或发布说明尚不存在。

- [ ] **Step 2: 增加仅服务器发布脚本与中文部署说明**

从 5.2.2 的凭据隔离和白名单打包逻辑派生 5.2.3，包名使用 `dnf-cloud-match-server-production-5.2.3.zip`，客户端版本仍标记 5.2.2，并加入 GeoIP 运行时依赖安装说明。

- [ ] **Step 3: 跑完整验证**

Run:

```bash
npm test -- --run
npm run build
npm run typecheck:test
git -c core.safecrlf=false diff --check
powershell -ExecutionPolicy Bypass -File scripts/package-production-5.2.3.ps1 -ValidateOnly
```

Expected: 全套测试、构建、类型检查和打包校验通过，`diff --check` 无错误。

- [ ] **Step 4: 生成并校验服务器 ZIP**

Run: `powershell -ExecutionPolicy Bypass -File scripts/package-production-5.2.3.ps1 -OutputDirectory deployment-packages`

Expected: 生成新的 5.2.3 ZIP 与 SHA-256 旁车文件；ZIP 内无数据库、主密钥或管理员密码。

- [ ] **Step 5: 记录证据、提交并推送**

将计划复选框和验证结果更新为实际值，然后运行：

```bash
git add DNFGameCapture/docs/superpowers/plans/2026-09-13-server-side-broadcaster-attribution.md DNFGameCapture/scripts/package-production-5.2.3.ps1 DNFGameCapture/cloud-match-server/README-production-5.2.3.md DNFGameCapture/cloud-match-server/tests/production-package.test.ts DNFGameCapture/cloud-match-server/tests/production-deployment.test.ts
git commit -m "发布服务器 5.2.3 主播归属管理"
git push origin codex/batch-library-conflict-resolution-5.2.2
```

Expected: 远端分支与本地 HEAD 一致，并向用户提供 ZIP 路径、SHA-256 和从 `/root` 解压部署的准确命令。
