# 本地选手库规范化与索引设计

**日期：** 2026-09-07
**状态：** 待用户评审
**适用版本：** 新客户端测试版，正式版采用灰度切换

## 1. 背景

当前客户端把 `alias_db.ini` 直接读成 `选手名称 -> 游戏ID字符串` 的映射。多个别名共享同一组游戏ID时，完整的游戏ID文本会在每个名称后重复保存。`player_identity_groups.json` 只记录身份组关系和解除归并快照，不是去重后的ID仓库。

当前身份分析还会对名称两两比较，并在比较过程中反复构造去重列表。文件读取本身很快，真正的风险是全量分析和 Web 状态构建在 UI 线程执行。

对现有样本库的统计基线为：122 个名称、985 次ID出现、约466个不同的规范化ID、519次重复出现。迁移后的验收必须保证每个原始名称和每个有效ID都能恢复，不能只验证文件大小。

## 2. 目标与非目标

### 目标

- 用 SQLite 作为本地选手库的唯一真实数据源。
- 同一种规范化游戏ID或冒险团ID只保存一份完整文本。
- 用内存反向索引完成快速识别，避免每次扫描所有名称。
- 保留选手名称、别名、手动归并、解除归并和历史快照能力。
- 游戏ID优先匹配，只有游戏ID没有结果时才匹配冒险团ID。
- 所有数据库读写、索引构建和身份分析都不阻塞 UI 线程。
- 测试阶段保留 `alias_db.ini` 兼容导出和自动回滚能力。

### 非目标

- 不改变比赛快照、比分、击杀、死亡和最近识别协议。
- 不把身份组元数据上传云端。
- 不凭单个冒险团ID自动合并两个选手。
- 不用模糊相似度直接覆盖已有身份关系。

## 3. 总体架构

```text
旧 alias_db.ini + player_identity_groups.json
                    |
                    | 一次性校验迁移
                    v
             player_library.db
                    |
          PlayerLibraryStore 工作线程
                    |
          不可变 PlayerLibrarySnapshot
             /          |           \
       gameIndex   adventureIndex   identityIndex
             \          |           /
                OCR / Web / C++ UI

player_library.db --成功提交后--> 可选兼容导出 alias_db.ini
```

SQLite 使用系统内嵌的 `sqlite3.c/sqlite3.h` 静态编译进 C++ 客户端，不要求用户安装数据库服务、DLL 或运行时组件。数据库放在 `%APPDATA%\\DNFGameCapture\\player_library.db`，避免 `Program Files` 写入权限问题。

## 4. 数据模型

内部使用 `entity_id` 表示一个逻辑身份，但界面仍只显示“选手”和“别名”，不显示“主选手”。未归并的每个名称初始拥有独立实体。

```sql
CREATE TABLE library_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE player_entities (
  entity_id INTEGER PRIMARY KEY,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  deleted_at INTEGER
);

CREATE TABLE player_names (
  name_id INTEGER PRIMARY KEY,
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  display_name TEXT NOT NULL,
  normalized_name TEXT NOT NULL UNIQUE,
  created_at INTEGER NOT NULL,
  deleted_at INTEGER
);

CREATE TABLE identifiers (
  identifier_id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL CHECK(kind IN ('game', 'adventure')),
  canonical_key TEXT NOT NULL,
  display_text TEXT NOT NULL,
  region TEXT NOT NULL DEFAULT '',
  job TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  UNIQUE(kind, canonical_key)
);

CREATE TABLE identifier_spellings (
  identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),
  raw_text TEXT NOT NULL,
  last_seen_at INTEGER NOT NULL,
  PRIMARY KEY(identifier_id, raw_text)
);

CREATE TABLE entity_identifiers (
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),
  link_state TEXT NOT NULL CHECK(link_state IN ('legacy', 'confirmed', 'candidate')),
  source TEXT NOT NULL,
  updated_at INTEGER NOT NULL,
  PRIMARY KEY(entity_id, identifier_id)
);

CREATE TABLE identity_groups (
  group_id TEXT PRIMARY KEY,
  group_type TEXT NOT NULL CHECK(group_type IN ('manual', 'automatic')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  dismissed_at INTEGER
);

CREATE TABLE identity_group_members (
  group_id TEXT NOT NULL REFERENCES identity_groups(group_id),
  entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),
  PRIMARY KEY(group_id, entity_id)
);

CREATE TABLE merge_operations (
  operation_id INTEGER PRIMARY KEY,
  group_id TEXT NOT NULL,
  before_snapshot_json TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  reverted_at INTEGER
);
```

`identifiers` 是去重核心。同一个ID出现在多个未归并名称下时，仍只创建一行 `identifiers`，通过多行 `entity_identifiers` 表示当前存在多个候选归属。手动归并后，名称归入同一实体，关联自然只剩一份。

`identifier_spellings` 只保留不同的原始显示写法，用于显示和审计，不参与重复匹配。`kind` 必须参与唯一键，防止游戏ID和冒险团ID文字相同却被错误合并。

## 5. 规范化规则

规范化器必须有版本号，初版复用现有 `DnfAliasSameStorageEntry` 语义：

- 去除首尾空白，统一 Unicode NFC。
- 统一全角和半角井号的解析方式。
- 保留职业后缀的区分规则；不同职业后缀不能静默合并。
- 保留完整显示文本，规范化结果只用于 `canonical_key`。
- 初版不额外进行大小写折叠，除非测试证明当前客户端把大小写视为同一ID。
- 游戏ID和冒险团ID使用不同 `kind` 命名空间。

同一个规范化键重复导入时执行 `INSERT ... ON CONFLICT(kind, canonical_key)`，只更新较完整的显示文本和最后出现时间，不新增第二个ID实体。

## 6. 迁移与回滚

首次启动检测 `library_meta.storage_version`。当不存在新数据库时按以下顺序迁移：

1. 定位 EXE 同目录的旧 `alias_db.ini` 和 `player_identity_groups.json`。
2. 将两个原文件复制为带时间戳的 `.migrated.bak`，原文件保持不变。
3. 创建 `player_library.db.tmp`，开启外键约束和事务。
4. 使用现有解析规则导入名称、ID、原始写法和身份组快照。
5. 对重复ID建立一行 `identifiers` 和多条关联，不静默丢弃任何名称。
6. 根据当前自动归类规则生成自动身份组记录；自动组不直接覆盖原始归属。
7. 校验名称数量、有效ID数量、关联数量和迁移指纹。
8. 提交事务并通过 `MoveFileEx` 原子替换为 `player_library.db`。
9. 写入 `storage_version=2`，构建内存索引。
10. 仅在数据库成功提交后生成兼容 `alias_db.ini`。

任何一步失败都删除临时数据库，保留原 INI 和备份，并继续使用旧读取路径。迁移日志必须包含失败阶段和计数，但不得包含授权码、设备令牌或完整隐私数据。

测试版配置为：

```ini
[PlayerLibrary]
StorageVersion=2
LegacyExportEnabled=1
```

`alias_db.ini` 在此模式下是兼容导出，不是主库。稳定后可以关闭导出，但至少保留迁移备份供回滚。单纯手工把 INI 改成 JSON 或 SQLite 不允许，因为旧加载器会将其视为空库。

## 7. 内存索引与识别算法

工作线程从数据库生成不可变快照：

```cpp
struct PlayerLibrarySnapshot {
    uint64_t revision;
    std::vector<PlayerEntity> entities;
    std::unordered_map<NameKey, EntityId> nameIndex;
    std::unordered_map<CanonicalKey, std::vector<EntityId>> gameIndex;
    std::unordered_map<CanonicalKey, std::vector<EntityId>> adventureIndex;
    std::unordered_map<EntityId, std::vector<IdentifierId>> entityIdentifiers;
};
```

识别流程固定为：

```text
OCR解析游戏ID
  -> 规范化
  -> 查询 gameIndex
  -> 唯一候选：采用
  -> 多候选：用职业、大区、固定位置、时间缓存二次判断
  -> 无候选：解析并查询 adventureIndex
  -> 冒险团多候选：报告歧义，不自动修改比赛数据
  -> 都没有：进入现有模糊/时序匹配器
```

游戏ID有结果时，冒险团ID不能覆盖游戏ID结果。两个字段指向不同实体时记录冲突并保持原数据不变。

查询平均复杂度从全库扫描的 `O(名称数 × ID数)` 降为哈希索引平均 `O(1)`，只在候选冲突时运行现有上下文评分。

## 8. 身份分析算法

### 完全相同集合

为每个实体的有效游戏ID生成排序后的唯一键序列，再计算签名：

```text
sort(unique(gameIdentifierIds)) -> A|B|C
signatureMap[A|B|C] -> entity list
```

同一签名的名称显示为完全一致组。空集合不参与自动归类。

### 重叠计数

使用反向索引而不是名称两两比较：

```text
for identifier in gameIndex:
    owners = distinct entities linked to identifier
    for each pair (a, b) in owners:
        common[a, b] += 1
```

共同游戏ID达到1个生成建议，达到4个进入自动归类候选。一个非常常见的ID导致候选组合过多时，只作为弱证据，不能单独触发自动合并。

自动归类不得因为传递链误合并。A-B和B-C达到阈值但A-C完全不同的情况保持分组候选，要求用户确认或满足组内一致性检查。

冒险团ID只用于识别兜底和辅助展示，不单独触发自动合并。

## 9. 归并、别名和解除

手动归并在单个 SQLite 事务中完成：

1. 校验 Web 命令携带的 `revision` 未过期。
2. 保存参与名称和关联ID的归并前快照。
3. 将名称归入目标实体，合并关联并通过唯一键去重。
4. 写入 `merge_operations` 和身份组关系。
5. 提交后只重建受影响的索引桶。
6. 发布新的不可变快照和 Web 状态。

新增别名只新增一条 `player_names`，不复制整个ID列表。编辑组内ID只修改 `entity_identifiers`，所有名称立即共享结果。删除别名只解除名称关联，不删除比赛数据或云端快照。

解除归并从 `before_snapshot_json` 恢复。若归并后产生了新ID，界面必须让用户选择保留在某个实体或放入“待分配”，不能静默丢弃。

## 10. 云端同步与兼容导出

新客户端从实体快照生成现有 v2 `entities` payload：每个实体发送一次名称集合、唯一游戏ID集合和唯一冒险团ID集合，不发送身份组元数据。

云端拉取时先在事务中按 `kind + canonical_key` 合并到本地库，再更新索引和兼容导出。冲突ID保留为候选并显示冲突，不直接覆盖本地确认关系。

若仍需支持旧云函数，只有在请求边界临时生成旧的“名称 -> ID列表”结构。旧格式可能重复写同一个ID，这是兼容协议的限制，不会反向污染 SQLite 主库。

数据库提交成功后异步生成 `alias_db.ini`：

- 使用临时文件写入并原子替换。
- 导出失败不回滚已提交数据库，只记录状态并在下次修改或启动时重试。
- 新客户端不从导出文件回读，除非用户明确执行“从旧库重新导入”。

## 11. 线程模型

新增 `PlayerLibraryStore`，SQLite 连接只由库工作线程使用。UI线程只提交命令和接收完成通知。工作线程负责：

- 数据库事务。
- 迁移和兼容导出。
- 索引构建。
- 身份建议计算。
- Web 状态 JSON 生成。

完成后通过 `WM_PLAYER_LIBRARY_UPDATED` 交换 `shared_ptr<const PlayerLibrarySnapshot>`。OCR线程和UI线程只读快照，不持有数据库锁，也不在高频识别路径写盘。快速连续编辑使用约200毫秒合并窗口，最终只重建一次受影响索引。

## 12. 分阶段发布

### 阶段一：影子迁移

只读旧库并生成新库，对比名称、ID和识别候选结果；不改变实际识别。迁移失败自动回退。

### 阶段二：索引识别

启用新索引查询，保留旧匹配器作为回退；记录两套结果不一致的案例。

### 阶段三：新库写入

身份管理、别名编辑、归并和云端序列化全部切换到 SQLite，保留兼容导出。

### 阶段四：关闭兼容导出

测试服稳定运行并确认不再需要旧客户端后，关闭 `LegacyExportEnabled`，继续保留备份文件和手动导入工具。

## 13. 测试与验收

- 迁移前后名称、ID、关联和身份组快照完整一致。
- 同一游戏ID在 SQLite `identifiers` 中只有一行。
- 游戏ID优先于冒险团ID，多个候选不自动改比分。
- 完全一致集合自动显示，空集合不显示。
- 一个共同ID只生成建议，四个共同ID进入自动归类候选。
- 自动归类不会发生传递链误合并。
- 手动归并、添加别名、组内编辑和解除归并可恢复。
- 数据库提交中断、导出失败、磁盘空间不足时原库不损坏。
- 旧 INI 保持可回滚，兼容导出内容可被旧客户端读取。
- 识别查询不扫描全量名称，库线程工作期间 UI、OCR和比赛记分保持响应。
- 使用现有样本和生成的10倍、100倍压力数据测试启动索引时间、识别延迟和内存占用。
- 运行 C++ Release x64 构建、数据库单元测试、迁移测试、身份分析测试、Web 状态测试和 `git diff --check`。

## 14. 明确结论

本设计不直接删除 `alias_db.ini`。测试阶段它是自动生成的兼容副本，新客户端真正读写 `player_library.db`。只有在新库经过影子迁移、索引识别和回滚测试后，才关闭兼容导出。这样解决的是逻辑重复、全量计算和 UI 卡顿，同时保留现有用户数据的安全退路。
