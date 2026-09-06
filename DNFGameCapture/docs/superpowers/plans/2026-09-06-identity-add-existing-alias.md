# 已有选手名称添加别名实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 添加别名时发现名称已存在，先展示原有游戏ID与合并后的去重列表，确认后再把已有名称并入当前身份组。

**Architecture:** Web 端使用当前 `playerIdentityState` 做无副作用预览；名称不存在时继续发送 `cmd_identity_add_alias`，名称存在时改为确认后发送 `cmd_identity_merge`。C++ 现有校验、并集合并、保存和失败回滚逻辑保持不变。

**Tech Stack:** 原生 JavaScript、HTML 弹窗、C++/MFC、现有 `nlohmann::json` WebView 消息桥。

---

### Task 1: 增加已有名称预览回归检查

**Files:**
- Create: `scripts/check-identity-add-existing-alias.ps1`
- Modify: `web前端/main.js`

- [x] **Step 1: 写失败检查**
  检查脚本要求新增的已有名称查找、合并预览和 `cmd_identity_merge` 分支。

- [x] **Step 2: 运行检查确认失败**
  运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-identity-add-existing-alias.ps1`，预期因新标记尚不存在而失败。

### Task 2: 实现确认和合并预览

**Files:**
- Modify: `web前端/main.js`
- Modify: `web前端/style.css`

- [x] **Step 1: 保持新名称路径**
  名称不在 `playerIdentityState.entries` 中时继续发送 `cmd_identity_add_alias`。

- [x] **Step 2: 增加已有名称路径**
  找到已有名称后，收集当前组与已有名称的 ID，使用现有 `buildIdentityMergePreview()` 展示原集合和去重并集；确认后发送包含两组名称的 `cmd_identity_merge`。

- [x] **Step 3: 处理边界状态**
  已在当前组内时提示无需重复添加；名称冲突或状态过期由 C++ 原有错误消息处理。

### Task 3: 验证

- [x] **Step 1:** 运行新增静态检查。
- [x] **Step 2:** 运行 `node --check web前端\main.js` 和 `node --check web前端\kill.js`。
- [x] **Step 3:** 运行 `git diff --check`。
- [ ] **Step 4:** 手动验证不存在名称、已有独立名称、已有其他身份组名称、当前组内名称和取消确认五条路径。
