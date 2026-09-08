# 本地选手库规范化与索引 Implementation Plan

实施中的兼容性修正、实际文件划分和验证结果见 [实施记录](2026-09-07-local-player-library-execution.md)。以下步骤保留为原始计划，不代表每条示例命令均原样执行。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** 将客户端的选手/别名库迁移到本地 SQLite 真实库，以唯一保存游戏 ID 和冒险团 ID，使用内存反向索引完成快速识别，同时保留 alias_db.ini 的迁移备份和兼容导出。

**Architecture:** PlayerLibraryDatabase 只负责 SQLite 连接、事务和迁移，PlayerLibraryStore 以单独工作线程串行执行数据库操作并发布不可变 PlayerLibrarySnapshot。主窗口只持有快照和短期兼容投影，OCR、Web 和云端编码都从快照读取；所有写入先变成库命令，再由工作线程提交并通过窗口消息回传。身份组仍是本地元数据，云端只收到已有 v2 entities 数据或请求边界生成的旧扁平格式。

**Tech Stack:** Visual Studio 2026 / MSVC v145、C++17、MFC/Win32、SQLite amalgamation 3.46.1 静态编译、现有 nlohmann::json、WebView2、PowerShell 和现有 Node/Vitest 服务端测试。

---

## 文件职责与边界

- Create PlayerLibraryModel.h/.cpp: 可脱离 MFC 测试的类型、Unicode/ID 规范化、唯一化、索引构建、游戏 ID 优先查询和身份重叠分析。
- Create PlayerLibraryDatabase.h/.cpp: SQLite schema、事务、迁移、合并/解除归并、快照读取和兼容 INI 原子导出。
- Create PlayerLibraryStore.h/.cpp: SQLite 工作线程、命令队列、200 ms 写入合并窗口、代际号和结果事件。
- Create PlayerLibraryCloudCodec.h/.cpp: entities v2 与旧 players 扁平格式之间的边界转换、大小/重复/冲突校验。
- Create third_party/sqlite/sqlite3.c, third_party/sqlite/sqlite3.h, third_party/sqlite/README.md: 固定版本的 SQLite amalgamation 和来源记录。
- Modify DNFGameCaptureDlg.h/.cpp: 生命周期、Web 命令、快照交换、旧 map 兼容投影和云端调用接入。
- Modify DNFGameCaptureDlg_IdentityPatch.cpp: OCR 身份查询切换到游戏 ID/冒险团 ID反向索引。
- Modify PlayerIdentityGroupService.h/.cpp: 保留已有公开 API，改用共享规范化规则并增加基于 ID 反向桶的分析入口。
- Modify DNFGameCapture.vcxproj and DNFGameCapture.vcxproj.filters: 加入 SQLite 和四个本地库源文件，SQLite 源文件禁用预编译头并按 C 编译。
- Modify web前端/index.html, web前端/main.js, web前端/style.css: 保持现有身份管理操作，增加库加载状态、冒险团 ID 编辑和冲突提示；不把身份组元数据放入云端 payload。
- Create scripts/player_library_model_test.cpp, scripts/player_library_database_test.cpp, scripts/player_library_migration_test.cpp, scripts/player_library_store_test.cpp, scripts/player_library_index_stress_test.cpp, scripts/check-player-library-normalization.ps1, scripts/check-player-library-cloud-boundary.js。
- Modify README.md: 记录新数据库位置、迁移备份、兼容导出和失败回退规则。

## 共享接口约定

所有后续任务使用同一组名称和字段，不再出现 mainId、aliasId 之类第二套术语：

~~~cpp
namespace dnf::player_library {

enum class IdentifierKind { game, adventure };
using EntityId = std::int64_t;
using IdentifierId = std::int64_t;

struct IdentifierValue {
    IdentifierKind kind = IdentifierKind::game;
    IdentifierId identifierId = 0;
    std::string canonicalKey;
    std::wstring displayText;
    std::wstring region;
    std::wstring job;
};

struct PlayerEntitySnapshot {
    EntityId entityId = 0;
    std::vector<std::wstring> names;
    std::vector<IdentifierValue> gameIds;
    std::vector<IdentifierValue> adventureGroupIds;
};

struct PlayerLibrarySnapshot {
    std::uint64_t revision = 0;
    std::vector<PlayerEntitySnapshot> entities;
    std::unordered_map<std::string, std::vector<EntityId>> gameIndex;
    std::unordered_map<std::string, std::vector<EntityId>> adventureIndex;
    std::unordered_map<std::wstring, EntityId> nameIndex;
};

struct LookupResult {
    std::vector<EntityId> candidateEntityIds;
    IdentifierKind matchedBy = IdentifierKind::game;
    bool usedAdventureFallback = false;
    bool conflict = false;
};

std::wstring NormalizeIdentifierText(std::wstring_view value);
std::string CanonicalIdentifierKey(std::wstring_view value, IdentifierKind kind);
bool EquivalentGameId(std::wstring_view left, std::wstring_view right);
std::vector<std::wstring> UniqueIdentifiers(
    const std::vector<std::wstring>& values, IdentifierKind kind);
LookupResult LookupIdentity(const PlayerLibrarySnapshot& snapshot,
    const std::vector<std::wstring>& gameIds,
    const std::vector<std::wstring>& adventureGroupIds);

} // namespace dnf::player_library
~~~

PlayerIdentityGroupService 继续导出现有 AliasEntry、IdentityAnalysis、Analyze、UnionIds 和 ComputeAliasEntriesFingerprint；现有测试调用方式不变。新增入口返回按规范化 ID 去重后的候选，不改变四个共同游戏 ID自动归类阈值。

Task 3 Step 4 中的 MergeEntitiesCommand、AddNameCommand、UpdateIdentifiersCommand、UnmergeCommand、DeleteNameCommand、ImportCloudCommand 和 LibraryCommand 会定义在 PlayerLibraryDatabase.h 的 PlayerLibraryDatabase 类声明之前；Task 4 和主窗口直接复用这组完整类型，不另建第二套命令定义。

### Task 1: 建立纯模型、规范化器和反向索引测试

**Files:**
- Create: PlayerLibraryModel.h
- Create: PlayerLibraryModel.cpp
- Modify: PlayerIdentityGroupService.h
- Modify: PlayerIdentityGroupService.cpp
- Create: scripts/player_library_model_test.cpp
- Modify: scripts/player_identity_group_test.cpp

- [ ] **Step 1: 写失败测试，固定规范化和查询契约**

在 scripts/player_library_model_test.cpp 覆盖：同一游戏 ID的相同写法只保留一份；不同职业后缀不合并；游戏 ID有候选时不查询冒险团桶；只有冒险团命中且候选超过一个时返回 conflict=true；空输入返回空候选。

~~~cpp
Require(UniqueIdentifiers({L" 白羽#剑魂 ", L"白羽#剑魂"},
    IdentifierKind::game).size() == 1, "duplicate id");
Require(!EquivalentGameId(L"白羽#剑魂", L"白羽#狂战"),
    "different jobs");
auto result = LookupIdentity(snapshot, {L"A"}, {L"团A"});
Require(result.candidateEntityIds == std::vector<EntityId>{1},
    "game id wins");
~~~

- [ ] **Step 2: 运行测试确认接口尚不存在**

~~~powershell
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_model_test.cpp PlayerLibraryModel.cpp /Fe:build\player_library_model_test.exe
~~~

Expected: 编译失败，提示 PlayerLibraryModel.h 或其函数不存在。

- [ ] **Step 3: 实现规范化器和索引查询**

在 PlayerLibraryModel.cpp 使用 Win32 NormalizeString(NormalizationC, ...) 完成 NFC；失败时回退到去首尾空白的原文。CanonicalIdentifierKey 只对 ASCII 大小写折叠，不对中文做模糊替换；半角 # 和全角 ＃的职业解析沿用当前 DnfFindAliasSharp 语义，带职业后缀的游戏 ID将后缀纳入 key。显示文本保留最完整写法：先比较是否带职业/大区，再比较长度，最后用稳定字典序。

~~~cpp
std::wstring NormalizeIdentifierText(std::wstring_view value)
{
    std::wstring trimmed(value);
    TrimAsciiAndUnicodeWhitespace(trimmed);
    if (trimmed.empty()) return {};
    const int required = NormalizeString(NormalizationC, trimmed.data(),
        static_cast<int>(trimmed.size()), nullptr, 0);
    if (required <= 0) return trimmed;
    std::wstring normalized(static_cast<std::size_t>(required), L'\0');
    if (NormalizeString(NormalizationC, trimmed.data(),
        static_cast<int>(trimmed.size()), normalized.data(), required) <= 0)
        return trimmed;
    return normalized;
}

std::string CanonicalIdentifierKey(std::wstring_view value,
    IdentifierKind kind)
{
    std::string key = Utf8FromWide(NormalizeIdentifierText(value));
    for (char& ch : key)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return (kind == IdentifierKind::game ? "game:" : "adventure:") + key;
}
~~~

UniqueIdentifiers 按首次出现顺序返回；重复项替换为信息更完整的显示写法。LookupIdentity 先查 gameIndex，游戏候选非空时永远不查 adventureIndex；候选超过一个时只报告冲突，不选择实体。

- [ ] **Step 4: 改造现有身份服务并运行两个纯测试**

将 PlayerIdentityGroupService 内部的重复比较改为共享规范化适配器，同时保留测试传入的 IdEquivalent 回调。使用 gameIndex 反向桶累计共同 ID；共同 1 个生成建议，共同 4 个进入自动组候选；A-B 和 B-C 达阈值但 A-C 不一致时不组成一个传递组。

~~~powershell
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_model_test.cpp PlayerLibraryModel.cpp PlayerIdentityGroupService.cpp /Fe:build\player_library_model_test.exe
build\player_library_model_test.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_identity_group_test.cpp PlayerIdentityGroupService.cpp PlayerLibraryModel.cpp /Fe:build\player_identity_group_test.exe
build\player_identity_group_test.exe
~~~

Expected: 分别输出 Player library model tests passed. 和 Player identity group tests passed.

- [ ] **Step 5: 提交纯模型阶段**

~~~powershell
git add PlayerLibraryModel.h PlayerLibraryModel.cpp PlayerIdentityGroupService.h PlayerIdentityGroupService.cpp scripts/player_library_model_test.cpp scripts/player_identity_group_test.cpp
git commit -m "feat: add normalized player library model and indexes"
~~~

### Task 2: 引入静态 SQLite 并接入 Visual Studio 工程

**Files:**
- Create: third_party/sqlite/sqlite3.c
- Create: third_party/sqlite/sqlite3.h
- Create: third_party/sqlite/README.md
- Modify: DNFGameCapture.vcxproj
- Modify: DNFGameCapture.vcxproj.filters
- Create: scripts/sqlite_smoke_test.cpp

- [ ] **Step 1: 固定 SQLite 来源和编译选项**

下载 SQLite 3.46.1 amalgamation sqlite-amalgamation-3460100.zip，只提取 sqlite3.c 和 sqlite3.h 到 third_party/sqlite/。README.md 写入版本、下载地址、下载文件 SHA-256、SQLite 公有领域声明和以下编译宏：

~~~text
SQLITE_THREADSAFE=1
SQLITE_DEFAULT_MEMSTATUS=0
SQLITE_OMIT_LOAD_EXTENSION=1
SQLITE_DQS=0
~~~

记录哈希：

~~~powershell
Get-FileHash third_party\sqlite\sqlite3.c -Algorithm SHA256; Get-FileHash third_party\sqlite\sqlite3.h -Algorithm SHA256
~~~

- [ ] **Step 2: 先写 SQLite smoke test**

scripts/sqlite_smoke_test.cpp 使用 sqlite3_open_v2 打开 :memory:，执行 CREATE TABLE、INSERT 和 sqlite3_close；SQL 返回码不是 SQLITE_OK 时打印 sqlite3_errmsg 并返回 1，成功时输出 SQLite smoke test passed.

- [ ] **Step 3: 把 SQLite 作为 C 源文件加入工程**

在 DNFGameCapture.vcxproj 头文件组加入 third_party\sqlite\sqlite3.h，源文件组加入：

~~~xml
<ClCompile Include="third_party\sqlite\sqlite3.c">
  <PrecompiledHeader>NotUsing</PrecompiledHeader>
  <CompileAs>CompileAsC</CompileAs>
  <PreprocessorDefinitions>SQLITE_THREADSAFE=1;SQLITE_DEFAULT_MEMSTATUS=0;SQLITE_OMIT_LOAD_EXTENSION=1;SQLITE_DQS=0;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  <WarningLevel>Level2</WarningLevel>
</ClCompile>
~~~

在 .filters 中把 h 放入“头文件”、c 放入“源文件”。不要把 sqlite3.h 放进 pch.h，也不要链接外部 sqlite3.lib。

- [ ] **Step 4: 编译 smoke test 和工程增量构建**

~~~powershell
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\sqlite_smoke_test.cpp third_party\sqlite\sqlite3.c /Fe:build\sqlite_smoke_test.exe
build\sqlite_smoke_test.exe
msbuild DNFGameCapture.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
~~~

Expected: smoke test 输出 SQLite smoke test passed.；Release 构建 0 error。

- [ ] **Step 5: 提交 SQLite 工程阶段**

~~~powershell
git add third_party/sqlite DNFGameCapture.vcxproj DNFGameCapture.vcxproj.filters scripts/sqlite_smoke_test.cpp
git commit -m "build: vendor sqlite amalgamation for local player library"
~~~

### Task 3: 实现数据库 schema、迁移和兼容导出

**Files:**
- Create: PlayerLibraryDatabase.h
- Create: PlayerLibraryDatabase.cpp
- Create: scripts/player_library_database_test.cpp
- Create: scripts/player_library_migration_test.cpp

- [ ] **Step 1: 固定数据库 API 并写失败测试**

PlayerLibraryDatabase.h 不依赖 MFC，使用以下类型和接口：

~~~cpp
struct DatabasePaths {
    std::wstring databasePath;
    std::wstring legacyAliasIniPath;
    std::wstring legacyIdentityGroupsPath;
};

struct MigrationReport {
    bool migrated = false;
    std::size_t nameCount = 0;
    std::size_t uniqueGameIdCount = 0;
    std::size_t uniqueAdventureIdCount = 0;
    std::size_t linkCount = 0;
    std::wstring backupPath;
    std::wstring failureStage;
};

class PlayerLibraryDatabase final {
public:
    ~PlayerLibraryDatabase();
    bool Open(const DatabasePaths& paths, std::wstring& error);
    bool InitializeOrMigrate(MigrationReport& report, std::wstring& error);
    bool ReadSnapshot(std::shared_ptr<const PlayerLibrarySnapshot>& snapshot,
        std::wstring& error) const;
    bool ApplyCommand(const LibraryCommand& command,
        std::shared_ptr<const PlayerLibrarySnapshot>& snapshot,
        std::wstring& error);
    bool ExportLegacyIni(const std::wstring& path,
        const PlayerLibrarySnapshot& snapshot, std::wstring& error) const;
    void Close() noexcept;
};
~~~

数据库测试先固定四个行为：同一 kind + canonicalKey 只有一行、不同 kind 不合并、软删除名称后可重新使用、事务失败后 revision 和关联不变。

- [ ] **Step 2: 创建 schema 和索引**

首次打开临时数据库时执行下面 schema，并设置 PRAGMA foreign_keys=ON、journal_mode=WAL、synchronous=NORMAL、busy_timeout=1500：

~~~sql
CREATE TABLE library_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE player_entities(
  entity_id INTEGER PRIMARY KEY, created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL, deleted_at INTEGER
);
CREATE TABLE player_names(
  name_id INTEGER PRIMARY KEY,
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  display_name TEXT NOT NULL, normalized_name TEXT NOT NULL,
  created_at INTEGER NOT NULL, deleted_at INTEGER
);
CREATE UNIQUE INDEX idx_player_names_active_norm
  ON player_names(normalized_name) WHERE deleted_at IS NULL;
CREATE INDEX idx_player_names_entity ON player_names(entity_id);
CREATE TABLE identifiers(
  identifier_id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL CHECK(kind IN ('game','adventure')),
  canonical_key TEXT NOT NULL, display_text TEXT NOT NULL,
  region TEXT NOT NULL DEFAULT '', job TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  UNIQUE(kind, canonical_key)
);
CREATE TABLE identifier_spellings(
  identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),
  raw_text TEXT NOT NULL, last_seen_at INTEGER NOT NULL,
  PRIMARY KEY(identifier_id, raw_text)
);
CREATE TABLE entity_identifiers(
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),
  link_state TEXT NOT NULL CHECK(link_state IN ('legacy','confirmed','candidate')),
  source TEXT NOT NULL, updated_at INTEGER NOT NULL,
  PRIMARY KEY(entity_id, identifier_id)
);
CREATE INDEX idx_entity_identifiers_identifier
  ON entity_identifiers(identifier_id, entity_id);
CREATE TABLE identity_groups(
  group_id TEXT PRIMARY KEY,
  group_type TEXT NOT NULL CHECK(group_type IN ('manual','automatic')),
  created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  dismissed_at INTEGER
);
CREATE TABLE identity_group_members(
  group_id TEXT NOT NULL REFERENCES identity_groups(group_id),
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  PRIMARY KEY(group_id, entity_id)
);
CREATE TABLE merge_operations(
  operation_id INTEGER PRIMARY KEY, group_id TEXT NOT NULL,
  before_snapshot_json TEXT NOT NULL, created_at INTEGER NOT NULL,
  reverted_at INTEGER
);
INSERT INTO library_meta(key,value) VALUES
 ('storage_version','2'), ('normalizer_version','1'), ('revision','1');
~~~

- [ ] **Step 3: 实现旧 INI/JSON 的安全迁移**

迁移读取 EXE 同目录 alias_db.ini 和 player_identity_groups.json，复制成带 YYYYMMDDHHMMSS 的 .migrated-时间戳.bak，创建 player_library.db.tmp，在事务中导入所有非空名称（包括空 ID 名称）、规范化 ID、原始 spelling 和已有手动组；校验名称、有效 ID、关联数量和迁移指纹；提交后用 MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) 原子替换正式数据库；最后导出兼容 INI。

每个旧 ID 只插入一个 identifiers 行，但为每个原始名称保留 entity_identifiers 关联。解析、事务、校验或替换失败时删除 .tmp，保留原 INI 和备份，并填写 failureStage；不得截断原 INI。数据库中保留空 ID 名称，兼容导出为匹配旧行为而省略空 ID 名称。

- [ ] **Step 4: 实现事务命令和原子兼容导出**

定义 LibraryCommand 为以下 std::variant：

~~~cpp
struct MergeEntitiesCommand {
    std::vector<EntityId> entityIds;
    std::uint64_t expectedRevision = 0;
};
struct AddNameCommand {
    EntityId entityId = 0;
    std::wstring name;
    std::uint64_t expectedRevision = 0;
};
struct UpdateIdentifiersCommand {
    EntityId entityId = 0;
    std::vector<std::wstring> gameIds;
    std::vector<std::wstring> adventureGroupIds;
    std::uint64_t expectedRevision = 0;
};
struct UnmergeCommand {
    std::string groupId;
    std::vector<EntityId> entityIds;
    bool splitAll = false;
    std::uint64_t expectedRevision = 0;
};
struct DeleteNameCommand {
    EntityId entityId = 0;
    std::wstring name;
    std::uint64_t expectedRevision = 0;
};
struct ImportCloudCommand {
    std::string entitiesJson;
    std::uint64_t expectedRevision = 0;
};
using LibraryCommand = std::variant<MergeEntitiesCommand, AddNameCommand,
    UpdateIdentifiersCommand, UnmergeCommand, DeleteNameCommand,
    ImportCloudCommand>;
~~~

每个命令在一个 transaction 中校验 expectedRevision、更新 library_meta.revision、重建受影响关联并读取新快照；失败 rollback。导出先写 alias_db.ini.tmp，Flush/Close 后 MoveFileExW 替换；导出失败只记录错误，不回滚已提交数据库。

- [ ] **Step 5: 运行迁移和数据库测试**

~~~powershell
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_database_test.cpp PlayerLibraryDatabase.cpp PlayerLibraryModel.cpp third_party\sqlite\sqlite3.c /Fe:build\player_library_database_test.exe
build\player_library_database_test.exe
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_migration_test.cpp PlayerLibraryDatabase.cpp PlayerLibraryModel.cpp third_party\sqlite\sqlite3.c /Fe:build\player_library_migration_test.exe
build\player_library_migration_test.exe
~~~

Expected: 迁移前后名称、有效 ID、关联数量一致；重复游戏 ID 在数据库中只有一行；损坏输入不会改变旧 INI；导出文件可被现有 DnfParseAliasListString 读取。

- [ ] **Step 6: 提交数据库阶段**

~~~powershell
git add PlayerLibraryDatabase.h PlayerLibraryDatabase.cpp scripts\player_library_database_test.cpp scripts\player_library_migration_test.cpp
git commit -m "feat: add transactional local player library database and migration"
~~~

### Task 4: 加入工作线程、不可变快照和身份分析缓存

**Files:**
- Create: PlayerLibraryStore.h
- Create: PlayerLibraryStore.cpp
- Create: scripts/player_library_store_test.cpp
- Modify: DNFGameCapture.vcxproj
- Modify: DNFGameCapture.vcxproj.filters

- [ ] **Step 1: 固定 Store 事件和生命周期 API**

~~~cpp
enum class LibraryEventKind { initialized, committed, failed, stopped };

struct LibraryEvent {
    LibraryEventKind kind = LibraryEventKind::failed;
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    bool ok = false;
    std::wstring message;
    MigrationReport migration;
    std::shared_ptr<const PlayerLibrarySnapshot> snapshot;
};

class PlayerLibraryStore final {
public:
    using EventSink = std::function<void(LibraryEvent)>;
    explicit PlayerLibraryStore(EventSink sink);
    ~PlayerLibraryStore();
    bool Start(const DatabasePaths& paths, std::uint64_t generation);
    std::uint64_t Submit(LibraryCommand command, std::uint64_t generation);
    std::uint64_t Refresh(std::uint64_t generation);
    void CancelGeneration(std::uint64_t generation);
    void Stop();
    std::shared_ptr<const PlayerLibrarySnapshot> Snapshot() const;
};
~~~

工作线程是唯一调用 sqlite3_* 的线程；Snapshot 只在互斥锁内复制 shared_ptr，不返回可变对象。Stop 先设置取消标志、唤醒条件变量、等待数据库线程完成当前事务，再关闭 SQLite；网络线程不由 Store 等待。

- [ ] **Step 2: 写异步顺序和代际测试**

测试提交初始化、两个快速更新和一个过期 generation，断言 initialized 先于 committed；旧 generation 事件不会替换当前快照；事件回调发生在 Store 工作线程；同一实体在 200 ms 窗口内的连续更新只产生一个数据库提交。

- [ ] **Step 3: 实现工作队列和 200 ms 合并**

队列元素包含 requestId、generation、LibraryCommand 和入队时间。连续的 UpdateIdentifiersCommand 对同一 entity 合并为最后一组 ID，连续新增名称保留全部；窗口到期后在一个 transaction 中执行。每次提交从实体、名称和关联一次性建立 nameIndex、gameIndex、adventureIndex；索引 value 排序并去重，保证事件和 JSON 稳定。

- [ ] **Step 4: 接入身份分析缓存**

从快照生成 AliasEntry 时只遍历实体一次。完全相同集合使用排序后的 identifier ID 签名；重叠分析按 gameIndex 反向桶累计 pair count，不执行名称两两全扫描。缓存键为 snapshot.revision + normalizer_version + autoGroupPolicyVersion，缓存包含 exact groups、overlap suggestions 和冲突 ID。

- [ ] **Step 5: 构建和运行 Store 测试**

~~~powershell
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_store_test.cpp PlayerLibraryStore.cpp PlayerLibraryDatabase.cpp PlayerLibraryModel.cpp third_party\sqlite\sqlite3.c /Fe:build\player_library_store_test.exe
build\player_library_store_test.exe
msbuild DNFGameCapture.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
~~~

Expected: Store 测试报告 generation、合并窗口和不可变快照检查通过，工程构建 0 error。

- [ ] **Step 6: 提交 Store 阶段**

~~~powershell
git add PlayerLibraryStore.h PlayerLibraryStore.cpp scripts\player_library_store_test.cpp DNFGameCapture.vcxproj DNFGameCapture.vcxproj.filters
git commit -m "feat: add asynchronous player library store and immutable snapshots"
~~~

### Task 5: 把主窗口和 OCR 从 map 迁移到快照

**Files:**
- Modify: DNFGameCaptureDlg.h
- Modify: DNFGameCaptureDlg.cpp
- Modify: DNFGameCaptureDlg_IdentityPatch.cpp
- Modify: DNFGameCapture.vcxproj
- Modify: DNFGameCapture.vcxproj.filters

- [ ] **Step 1: 增加窗口消息、Store 成员和生命周期方法**

在 DNFGameCaptureDlg.h 增加：

~~~cpp
#include "PlayerLibraryStore.h"
#define WM_PLAYER_LIBRARY_EVENT (WM_USER + 122)

afx_msg LRESULT OnPlayerLibraryEvent(WPARAM wParam, LPARAM lParam);
void StartPlayerLibraryStore();
void QueuePlayerLibraryCommand(dnf::player_library::LibraryCommand command);
void ApplyPlayerLibrarySnapshot(std::shared_ptr<const dnf::player_library::PlayerLibrarySnapshot> snapshot);
void RebuildLegacyAliasProjection(const dnf::player_library::PlayerLibrarySnapshot& snapshot);
std::shared_ptr<const dnf::player_library::PlayerLibrarySnapshot> GetPlayerLibrarySnapshot() const;

std::unique_ptr<dnf::player_library::PlayerLibraryStore> m_playerLibraryStore;
std::shared_ptr<const dnf::player_library::PlayerLibrarySnapshot> m_playerLibrarySnapshot;
mutable std::mutex m_playerLibrarySnapshotMutex;
std::uint64_t m_playerLibraryGeneration = 1;
bool m_playerLibraryReady = false;
bool m_playerLibraryMigrationFailed = false;
~~~

消息映射加入 ON_MESSAGE(WM_PLAYER_LIBRARY_EVENT, &CDNFGameCaptureDlg::OnPlayerLibraryEvent)。Store 回调只 PostMessage，事件对象由窗口线程释放，不直接访问 MFC 控件。

- [ ] **Step 2: 把启动读取改为非阻塞初始化**

将 RunStartupStage 中的 LoadAliasDB 改为 StartPlayerLibraryStore：计算 %APPDATA%\DNFGameCapture\player_library.db，创建父目录，设置旧 INI 和组 JSON 路径，启动 Store 后立即返回。初始化完成前 Web 先收到：

~~~json
{
  "playerLibrary": {
    "ready": false,
    "storage": "sqlite-v2",
    "revision": 0,
    "message": "正在加载本地选手库"
  }
}
~~~

初始化失败时只记录 [PlayerLibrary] 日志并使用一次性的旧 INI 只读回退投影，m_playerLibraryMigrationFailed=true；不得在 UI 线程同步重试或弹阻塞窗口。

- [ ] **Step 3: 让快照成为唯一业务读取源**

OnPlayerLibraryEvent 收到 initialized/committed 后调用 ApplyPlayerLibrarySnapshot，短暂替换 m_aliasDB 为兼容投影，刷新 m_playerIdentityRevision、候选下拉框、最近选手和 Web。之后 m_aliasDB 只允许出现在 RebuildLegacyAliasProjection、旧回退和兼容导出中；新的读取通过：

~~~cpp
std::vector<CString> GetGameIdsForName(const CString& name) const;
std::vector<CString> GetAdventureIdsForName(const CString& name) const;
bool QueueNameIdentifiers(const CString& name,
    const std::vector<CString>& gameIds,
    const std::vector<CString>& adventureIds);
~~~

每次替换后执行 rg -n "m_aliasDB\s*\[|m_aliasDB\.erase|m_aliasDB\.clear" DNFGameCaptureDlg.cpp；结果只能位于兼容投影、旧回退和导出函数。

- [ ] **Step 4: 把 OCR 查询切换为游戏 ID优先、冒险团兜底**

在 DNFGameCaptureDlg_IdentityPatch.cpp 的候选构建中，把当前全量 m_aliasDB 遍历改为 LookupIdentity(*snapshot, parsedGameIds, parsedAdventureGroupIds)。唯一候选沿用职业、区域、固定位置评分；多个候选记录冲突并保持原选手不变；只有没有游戏 ID候选时才查冒险团 ID。Store 未就绪时使用旧匹配器，快照就绪后不再全量扫描。

- [ ] **Step 5: 处理退出和已有编辑路径**

在 DoRealExit 和析构函数中递增 m_playerLibraryGeneration，调用 CancelGeneration 后 Stop；所有 Web 命令、OCR 新增 ID、快速添加和树编辑改成 LibraryCommand。UI 不等待网络；数据库线程只在有限事务内等待。保存失败时从事件中的旧快照恢复 Web，不清除当前比赛比分、战绩或最近识别。

- [ ] **Step 6: 构建并做 map 扫描检查**

~~~powershell
rg -n "m_aliasDB\s*\[|m_aliasDB\.erase|m_aliasDB\.clear" DNFGameCaptureDlg.cpp
msbuild DNFGameCapture.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
~~~

Expected: 直接 map 写入只剩兼容投影/迁移回退实现；Release x64 0 error。

- [ ] **Step 7: 提交主窗口接入阶段**

~~~powershell
git add DNFGameCaptureDlg.h DNFGameCaptureDlg.cpp DNFGameCaptureDlg_IdentityPatch.cpp
git commit -m "feat: route player library reads through asynchronous sqlite snapshots"
~~~

### Task 6: 接入身份管理 Web 命令和本地归并事务

**Files:**
- Modify: DNFGameCaptureDlg.cpp
- Modify: DNFGameCaptureDlg_IdentityPatch.cpp
- Modify: web前端/index.html
- Modify: web前端/main.js
- Modify: web前端/style.css
- Create: scripts/check-player-library-normalization.ps1

- [ ] **Step 1: 统一生成身份状态 JSON**

BuildPlayerIdentityStateJson 从快照生成现有字段并增加：

~~~json
{
  "revision": 12,
  "storage": {"type":"sqlite-v2", "ready":true, "normalizerVersion":1},
  "entries": [
    {"name":"白羽", "ids":["A"], "adventureGroupIds":["团A"]}
  ],
  "groups": [],
  "exactMatches": [],
  "overlapSuggestions": [],
  "conflicts": {"gameIds":[], "adventureGroupIds":[]}
}
~~~

自动组只作为分析结果显示，不改变未手动归并实体的真实关联；手动组读取 identity_groups 和 merge_operations。所有数组按 canonical key、entity ID、名称排序。

- [ ] **Step 2: 改造 C++ 命令处理**

保留现有动作名 cmd_identity_refresh、cmd_identity_merge、cmd_identity_add_alias、cmd_identity_update_ids、cmd_identity_unmerge、cmd_identity_delete_alias、cmd_identity_ignore_overlap。Update 命令兼容旧 ids 字段，同时读取 gameIds、adventureGroupIds 和 revision：

~~~cpp
if (action == "cmd_identity_update_ids") {
    UpdateIdentifiersCommand command;
    command.entityId = ResolveEntityIdByName(name);
    command.gameIds = ReadStringArray(body, "gameIds",
        body.value("ids", json::array()));
    command.adventureGroupIds = ReadStringArray(
        body, "adventureGroupIds", json::array());
    command.expectedRevision = body.value(
        "revision", CurrentLibraryRevision());
    QueuePlayerLibraryCommand(std::move(command));
}
~~~

merge 保存 before snapshot 后由数据库事务移动名称到一个实体并建立 manual group；add_alias 只新增名称行，不复制 identifier 行；unmerge 用 before_snapshot_json 恢复；删除名称只软删除名称关联。revision 过期返回 library_revision_conflict，前端刷新而不是覆盖新数据。

- [ ] **Step 3: 调整 Web 编辑器**

在 web前端/index.html 身份详情区域增加冒险团 ID列表和库状态节点：

~~~html
<div id="player-library-status" class="player-library-status"
     aria-live="polite"></div>
<div class="identity-id-section">
  <div class="identity-detail-label">冒险团 ID</div>
  <div id="identity-adventure-id-editor"
       class="identity-id-editor-rows"></div>
</div>
~~~

main.js 保存时同时发送 gameIds、adventureGroupIds 和当前 revision，兼容旧宿主继续发送 ids；storage.ready=false 时显示加载状态，conflicts 显示“存在多个候选，未自动修改比赛数据”。不修改现有“选手/别名/游戏ID”术语。

- [ ] **Step 4: 增加静态检查并运行前端测试**

scripts/check-player-library-normalization.ps1 使用以下检查：

~~~powershell
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$required = @('PlayerLibraryModel.h','PlayerLibraryModel.cpp','PlayerLibraryDatabase.h','PlayerLibraryDatabase.cpp','PlayerLibraryStore.h','PlayerLibraryStore.cpp','web前端\index.html','web前端\main.js','web前端\style.css')
foreach ($relative in $required) {
  if (-not (Test-Path (Join-Path $root $relative))) { throw "Missing $relative" }
}
$cpp = Get-Content (Join-Path $root 'DNFGameCaptureDlg.cpp') -Raw
$js = Get-Content (Join-Path $root 'web前端\main.js') -Raw
if ($cpp -notmatch 'WM_PLAYER_LIBRARY_EVENT|PlayerLibraryStore') { throw 'C++ store bridge missing' }
if ($js -notmatch 'adventureGroupIds|player-library-status') { throw 'Web library state missing' }
Write-Output 'Player library normalization static checks passed.'
~~~

Run:

~~~powershell
node --check "web前端\main.js"; node --check "web前端\kill.js"; powershell -ExecutionPolicy Bypass -File "scripts\check-player-library-normalization.ps1"; powershell -ExecutionPolicy Bypass -File "scripts\check-player-identity-group.ps1"
~~~

Expected: all checks pass。

- [ ] **Step 5: 提交身份管理阶段**

~~~powershell
git add DNFGameCaptureDlg.cpp DNFGameCaptureDlg_IdentityPatch.cpp web前端/index.html web前端/main.js web前端/style.css scripts/check-player-library-normalization.ps1
git commit -m "feat: connect identity management to normalized player library"
~~~

### Task 7: 实现云端 v2/旧格式边界和本地库合并

**Files:**
- Create: PlayerLibraryCloudCodec.h
- Create: PlayerLibraryCloudCodec.cpp
- Modify: DNFGameCaptureDlg.cpp
- Create: scripts/check-player-library-cloud-boundary.js

- [ ] **Step 1: 固定云端编码接口**

~~~cpp
struct CloudCodecResult {
    bool ok = false;
    std::string payload;
    std::string error;
};
CloudCodecResult EncodeV2Entities(const PlayerLibrarySnapshot& snapshot);
CloudCodecResult EncodeLegacyPlayers(const PlayerLibrarySnapshot& snapshot,
    const std::vector<std::wstring>& pendingDeleteNames);
CloudCodecResult DecodeV2Entities(const std::string& json,
    std::vector<PlayerEntitySnapshot>& entities,
    std::uint64_t& revision);
~~~

v2 每个实体只发送一次 entityId、names、gameIds、adventureGroupIds；不发送 identity_groups、beforeMerge、autoSplitFingerprints、授权、设备令牌、截图或外观。旧格式只在请求边界把实体名称展开为 players，重复字符串只存在于网络兼容 payload，不回写 SQLite。

- [ ] **Step 2: 编写编码边界测试**

scripts/check-player-library-cloud-boundary.js 读取 C++ 源码和 fixture，断言 v2 字段存在、身份组字段不存在、legacy builder 只在兼容函数中生成 players；解析返回实体时校验最多 10000 个实体、每实体最多 32 个名称/256 个游戏 ID/256 个冒险团 ID、UTF-8 字符串非空且不含 NUL、总 payload 不超过 256 KiB。

- [ ] **Step 3: 替换现有 cloud builder 和 pull merge**

BuildAliasDbJsonPayload、BuildAliasDbAppendPayload 和 DnfSubmitV2PlayerLibrary 先从当前快照调用 codec。v2 服务器接收 entities；旧云函数接收扁平 players。收到公共库后先在 Store 中执行 ImportCloudCommand：

1. 事务内规范化 kind + canonicalKey 并合并 spelling。
2. 已确认实体不被另一个实体静默夺走；冲突写入 candidate link 和 Web conflicts。
3. 不删除本地名称或 ID，不清空手动删除列表。
4. 提交后刷新 m_aliasDB 兼容导出和当前上场选手的候选别名；不修改比分、战绩、最近识别事件或比赛快照。

旧 v2 响应缺少 adventureGroupIds 时按空数组处理；无效 JSON、过期 revision、超限响应或事务失败时原快照保持不变。

- [ ] **Step 4: 让 7 天自动同步和手动同步共享 codec**

StartAliasDbAutoSyncAttempt、StartManualAliasDbSync 和退出前旧自动上传只调用 codec/Store，不再复制 m_aliasDB 生成逻辑。追加模式只发送非空游戏 ID实体；旧云函数没有 aliasAppendSupported=true 时自动任务只拉取、不上传，保留现有日志和七天记账规则。

- [ ] **Step 5: 运行云端边界和现有服务端测试**

~~~powershell
node "scripts\check-player-library-cloud-boundary.js"; node --check "云函数\index.js"; Push-Location cloud-match-server; npm test -- --run; Pop-Location
~~~

Expected: 云端边界检查通过；现有 server Vitest 测试全部通过；不修改 cloud-match-server 或 云函数 文件。

- [ ] **Step 6: 提交云端边界阶段**

~~~powershell
git add PlayerLibraryCloudCodec.h PlayerLibraryCloudCodec.cpp DNFGameCaptureDlg.cpp scripts/check-player-library-cloud-boundary.js
git commit -m "feat: serialize normalized player library at cloud boundaries"
~~~

### Task 8: 回滚策略、压力测试和文档

**Files:**
- Create: scripts/player_library_index_stress_test.cpp
- Modify: scripts/check-player-library-normalization.ps1
- Modify: README.md
- Modify: DNFGameCaptureDlg.cpp
- Modify: DNFGameCaptureDlg.h

- [ ] **Step 1: 完成配置和迁移状态记录**

在 config.ini 的 [PlayerLibrary] 写入：

~~~ini
StorageVersion=2
LegacyExportEnabled=1
NormalizerVersion=1
~~~

缺少区段时默认 SQLite v2、启用兼容导出；数据库失败时当前运行回退旧只读 map，下次启动再次尝试迁移。成功数据库提交后异步导出 alias_db.ini；导出失败不阻塞 OCR 和 Web，只写 [PlayerLibrary] legacy export failed 日志。不要删除旧 INI、迁移备份或 player_identity_groups.json。

- [ ] **Step 2: 添加十倍/百倍索引压力测试**

player_library_index_stress_test.cpp 生成基线 122 个名称、466 个唯一游戏 ID，再生成 10 倍和 100 倍数据，分别建立快照并执行 100000 次游戏 ID查询。测试比较索引结果和线性参考结果完全一致，并输出数据量、建索引耗时、查询耗时、峰值实体数量；失败返回非零，不使用硬件相关毫秒阈值。

- [ ] **Step 3: 验证启动不在 UI 线程加载库**

在 RunStartupStage 和 LoadAliasDB 相关日志增加线程 ID与耗时；静态检查禁止 StartPlayerLibraryStore 后立即调用 BuildPlayerIdentityStateJson 的同步全量分析。Web 首帧允许 playerLibrary.ready=false，快照到达后再刷新身份面板。用 10000 次快速打开/关闭身份面板验证消息泵持续响应。

- [ ] **Step 4: 更新 README 和诊断信息**

README.md 增加真实库路径 %APPDATA%\DNFGameCapture\player_library.db；首次迁移保留原 alias_db.ini 并生成 .migrated-时间戳.bak；之后 alias_db.ini 是兼容导出；数据库只由 Store 工作线程访问；游戏 ID优先、冒险团 ID兜底；迁移/导出失败时本地 OCR、记分、TXT 输出和比赛快照继续工作。

- [ ] **Step 5: 运行完整静态和压力验证**

~~~powershell
node --check "web前端\main.js"; node --check "web前端\kill.js"; powershell -ExecutionPolicy Bypass -File "scripts\check-player-library-normalization.ps1"; powershell -ExecutionPolicy Bypass -File "scripts\check-player-identity-group.ps1"; git diff --check
cl /nologo /std:c++17 /EHsc /utf-8 /I. scripts\player_library_index_stress_test.cpp PlayerLibraryModel.cpp /Fe:build\player_library_index_stress_test.exe
build\player_library_index_stress_test.exe
~~~

Expected: 静态检查通过，压力测试输出两组规模和一致性通过，git diff --check 无输出。

- [ ] **Step 6: 提交回滚、压力和文档阶段**

~~~powershell
git add scripts/player_library_index_stress_test.cpp scripts/check-player-library-normalization.ps1 README.md DNFGameCaptureDlg.cpp DNFGameCaptureDlg.h
git commit -m "test: verify normalized player library rollback and index performance"
~~~

### Task 9: Release x64 发布和源/Release 校验

**Files:**
- Modify: x64\Release\web前端\index.html
- Modify: x64\Release\web前端\main.js
- Modify: x64\Release\web前端\style.css
- Generate: x64\Release\DNFGameCapture.exe
- Generate: x64\Release\DNFGameCapture.pdb

- [ ] **Step 1: 执行 Release x64 全量构建**

~~~powershell
msbuild DNFGameCapture.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
~~~

Expected: 0 error；允许已有未引用标签等 warning，但不得出现 SQLite、线程、WebView2 或字符集错误。

- [ ] **Step 2: 同步前端并校验 SHA-256**

~~~powershell
$release = Join-Path (Get-Location) 'x64\Release\web前端'
Copy-Item 'web前端\index.html' $release -Force
Copy-Item 'web前端\main.js' $release -Force
Copy-Item 'web前端\style.css' $release -Force
foreach ($name in @('index.html','main.js','style.css')) {
  $sourceHash = (Get-FileHash (Join-Path 'web前端' $name) -Algorithm SHA256).Hash
  $releaseHash = (Get-FileHash (Join-Path $release $name) -Algorithm SHA256).Hash
  if ($sourceHash -ne $releaseHash) { throw "SHA-256 mismatch: $name" }
  Write-Output "$name $sourceHash"
}
~~~

- [ ] **Step 3: 做最终文件、旧库和构建检查**

~~~powershell
if (-not (Test-Path 'x64\Release\DNFGameCapture.exe')) { throw 'missing Release exe' }
Get-FileHash 'x64\Release\DNFGameCapture.exe' -Algorithm SHA256
git diff --check
git status --short
~~~

确认 Release 目录存在 DNFGameCapture.exe、WebView2Loader.dll 和 web前端；不要把用户本机 player_library.db、授权文件或 alias_db.ini 打进安装包。

- [ ] **Step 4: 提交发布元数据**

~~~powershell
git add x64\Release\web前端\index.html x64\Release\web前端\main.js x64\Release\web前端\style.css
git commit -m "release: publish normalized player library client"
~~~

## 验收清单

- 迁移前后的名称、有效 ID、关联和手动身份组快照一致；原 alias_db.ini 未被删除或截断。
- SQLite identifiers 中同一 kind + canonicalKey 只有一行；兼容导出可以重复，但不反向污染数据库。
- OCR 查询使用游戏 ID反向索引，只有游戏 ID无候选时才查询冒险团 ID；多候选不自动改比分。
- 完全一致集合可显示，空集合不自动归类，1 个共同游戏 ID给建议，4 个共同游戏 ID进入自动候选且无传递链误合并。
- 手动归并、添加别名、组内编辑、删除名称和解除归并均为事务操作，revision 冲突不覆盖新数据。
- UI、OCR、记分、TXT 输出和比赛快照在数据库迁移失败、导出失败或云端不可用时继续运行。
- 启动读取、索引、身份分析和云端合并不在 UI 线程执行；连续编辑在 200 ms 内合并。
- v2 云端 payload 只含 entityId/names/gameIds/adventureGroupIds，不含身份组元数据、授权或设备信息；旧协议仍可在边界工作。
- Release x64 构建、Node 语法检查、身份/迁移/数据库/压力测试、git diff --check 和源/Release SHA-256 全部通过。
