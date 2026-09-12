# 5.2.2 Batch Library Conflict Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent stale client submissions at the source, report genuine pull conflicts, and let an administrator safely resolve all unique-name conflicts in one confirmed batch.

**Architecture:** Keep the 5.2.1 pull-before-push client flow, but submit the freshly persisted local snapshot even after a partial pull so the server receives the unresolved evidence. Add a pure server-side conflict grouping module, then execute selected associations/public merges in one guarded SQLite transaction with durable redirects and audit. Render server-computed groups in a responsive admin dialog whose “按名称一键合并” action selects only groups with exactly one canonical public target.

**Tech Stack:** C++17/MFC, WinHTTP, SQLite, Node.js 22, TypeScript, Express, Zod, better-sqlite3, Vitest, Playwright/Edge, PowerShell, MSBuild x64 Release.

---

## File Map

- Create `cloud-match-server/src/library-conflict-resolution.ts`: pure connected-component grouping, eligibility classification, and stable group tokens.
- Create `cloud-match-server/tests/library-conflict-resolution.test.ts`: focused unit coverage for unique target, public merge, ambiguity, aggregation, and token changes.
- Modify `cloud-match-server/src/db.ts`: add append-only batch conflict-resolution audit storage.
- Modify `cloud-match-server/src/library-admin-data.ts`: build global pending conflict state and execute a selected batch atomically.
- Modify `cloud-match-server/src/library-admin.ts`: validate and expose the batch resolution API; include groups in `/state`.
- Create `cloud-match-server/tests/library-batch-conflict-resolution.test.ts`: real Express/SQLite integration, redirect, pending rewrite, stale guard, and rollback coverage.
- Modify `cloud-match-server/src/library-admin-page.ts`: add the batch dialog, “按名称一键合并”, preview, target selection, progress, and responsive styling.
- Create `scripts/check-cloud-library-conflict-batch.cjs`: real Edge interaction and responsive screenshot regression.
- Modify `DNFGameCaptureDlg.cpp`: submit the persisted post-import snapshot after partial pulls and report the waiting-for-admin state without completing the seven-day checkpoint.
- Modify `scripts/player_library_push_integration_test.js`: static orchestration regression for partial pull submission and lifecycle guards.
- Modify `DNFGameCaptureDlg.h`, `DNFGameCapture.rc`, `scripts/DNFGameCapture-5.0.2.iss`, `web前端/index.html`, and `web前端/main.js`: 5.2.2 version/cache markers.
- Modify release checks under `scripts/`: align version assertions and client package naming.
- Create `cloud-match-server/README-production-5.2.2.md`, `scripts/package-production-5.2.2.ps1`, and `docs/release-5.2.2.md`: server-first deployment, rollback, and local artifact instructions.

## Task 1: Pure Conflict Grouping

**Files:**
- Create: `cloud-match-server/src/library-conflict-resolution.ts`
- Create: `cloud-match-server/tests/library-conflict-resolution.test.ts`

- [ ] **Step 1: Write the failing grouping tests**

Cover aggregation across submissions, NFC/case-normalized same names, a unique live target, multiple live targets, no live target, and stale-token sensitivity:

```ts
import { describe, expect, test } from 'vitest';
import { buildConflictResolutionGroups } from '../src/library-conflict-resolution.js';

const entity = (entityId: string, names: string[], gameIds: string[] = []) => ({ entityId, names, gameIds });
const submission = (id: number, revision: string, ...entities: ReturnType<typeof entity>[]) =>
  ({ id, submissionRevision: revision.repeat(64), entities });

test('groups every pending source with its one canonical public name owner', () => {
  const groups = buildConflictResolutionGroups({
    revision: 7,
    publicEntities: [entity('public-a', ['丁真'])],
    submissions: [
      submission(1, 'a', entity('local-a', [' 丁真 '], ['game-a'])),
      submission(2, 'b', entity('local-b', ['丁真'], ['game-b'])),
    ],
    conflicts: [{ kind: 'names', value: '丁真', entityIds: ['public-a', 'local-a', 'local-b'] }],
  });
  expect(groups).toHaveLength(1);
  expect(groups[0]).toMatchObject({ kind: 'unique_name_target', suggestedTargetEntityId: 'public-a', publicEntityIds: ['public-a'] });
  expect(groups[0].sources.map(source => source.entityId)).toEqual(['local-a', 'local-b']);
});

test('never marks multi-public or no-public components as one-click eligible', () => {
  const multi = buildConflictResolutionGroups({
    revision: 3,
    publicEntities: [entity('public-a', ['A']), entity('public-b', ['B'])],
    submissions: [submission(3, 'c', entity('local-c', ['A', 'B']))],
    conflicts: [
      { kind: 'names', value: 'A', entityIds: ['public-a', 'local-c'] },
      { kind: 'names', value: 'B', entityIds: ['public-b', 'local-c'] },
    ],
  });
  expect(multi).toHaveLength(1);
  expect(multi[0]).toMatchObject({ kind: 'public_merge', publicEntityIds: ['public-a', 'public-b'] });

  const none = buildConflictResolutionGroups({
    revision: 0,
    publicEntities: [],
    submissions: [submission(4, 'd', entity('local-d', ['Same'])), submission(5, 'e', entity('local-e', ['Same']))],
    conflicts: [{ kind: 'names', value: 'Same', entityIds: ['local-d', 'local-e'] }],
  });
  expect(none[0]).toMatchObject({ kind: 'ambiguous', publicEntityIds: [] });
});

test('changes the signed token when a library or submission revision changes', () => {
  const make = (revision: number, token: string) => buildConflictResolutionGroups({
    revision,
    publicEntities: [entity('public-a', ['A'])],
    submissions: [submission(1, token, entity('local-a', ['A']))],
    conflicts: [{ kind: 'names', value: 'A', entityIds: ['public-a', 'local-a'] }],
  })[0].token;
  expect(new Set([make(7, 'a'), make(8, 'a'), make(7, 'b')]).size).toBe(3);
});
```

- [ ] **Step 2: Run the focused test and verify RED**

Run from `cloud-match-server`:

```powershell
npm test -- --run tests/library-conflict-resolution.test.ts
```

Expected: FAIL because `../src/library-conflict-resolution.js` does not exist.

- [ ] **Step 3: Implement the pure grouping module**

Define stable transport types and build connected components from the authoritative combined-projection conflicts:

```ts
import { createHash } from 'node:crypto';
import type { PlayerEntity } from './player-library.js';

export type ConflictResolutionKind = 'unique_name_target' | 'public_merge' | 'ambiguous';
export interface ResolutionConflict { kind: 'names' | 'gameIds'; value: string; entityIds: string[] }
export interface ResolutionSubmission { id: number; submissionRevision: string; entities: PlayerEntity[] }
export interface ConflictResolutionSource extends PlayerEntity { submissionId: number; submissionRevision: string }
export interface ConflictResolutionGroup {
  token: string;
  kind: ConflictResolutionKind;
  conflictNames: string[];
  publicEntityIds: string[];
  sources: ConflictResolutionSource[];
  suggestedTargetEntityId?: string;
}

export function buildConflictResolutionGroups(input: {
  revision: number;
  publicEntities: PlayerEntity[];
  submissions: ResolutionSubmission[];
  conflicts: ResolutionConflict[];
}): ConflictResolutionGroup[] {
  const publicIds = new Set(input.publicEntities.map(entity => entity.entityId));
  const sources = new Map<string, ConflictResolutionSource[]>();
  for (const submission of input.submissions) for (const entity of submission.entities) {
    const rows = sources.get(entity.entityId) ?? [];
    rows.push({ ...entity, submissionId: submission.id, submissionRevision: submission.submissionRevision });
    sources.set(entity.entityId, rows);
  }
  const neighbors = new Map<string, Set<string>>();
  const connect = (left: string, right: string) => {
    const edges = neighbors.get(left) ?? new Set<string>();
    edges.add(right); neighbors.set(left, edges);
  };
  for (const conflict of input.conflicts) {
    const ids = [...new Set(conflict.entityIds)].sort();
    if (ids.length < 2) continue;
    for (const id of ids) { connect(ids[0], id); connect(id, ids[0]); }
  }
  const seen = new Set<string>(); const groups: ConflictResolutionGroup[] = [];
  for (const start of [...neighbors.keys()].sort()) {
    if (seen.has(start)) continue;
    const pending = [start]; seen.add(start); const component: string[] = [];
    while (pending.length) {
      const id = pending.pop()!; component.push(id);
      for (const next of [...(neighbors.get(id) ?? [])].sort()) if (!seen.has(next)) {
        seen.add(next); pending.push(next);
      }
    }
    component.sort(); const memberIds = new Set(component);
    const publicEntityIds = component.filter(id => publicIds.has(id));
    const groupSources = component.flatMap(id => sources.get(id) ?? [])
      .sort((a, b) => a.submissionId - b.submissionId || a.entityId.localeCompare(b.entityId));
    const conflictNames = [...new Set(input.conflicts
      .filter(conflict => conflict.entityIds.some(id => memberIds.has(id)))
      .map(conflict => conflict.value))].sort((a, b) => a.localeCompare(b));
    const kind: ConflictResolutionKind = publicEntityIds.length === 1 &&
      groupSources.some(source => source.entityId !== publicEntityIds[0]) ? 'unique_name_target' :
      publicEntityIds.length >= 2 ? 'public_merge' : 'ambiguous';
    const signed = { revision: input.revision, kind, component, publicEntityIds, conflictNames,
      sources: groupSources.map(source => ({ submissionId: source.submissionId,
        submissionRevision: source.submissionRevision, entityId: source.entityId })) };
    groups.push({ token: createHash('sha256').update(JSON.stringify(signed)).digest('hex'), kind,
      conflictNames, publicEntityIds, sources: groupSources,
      ...(kind === 'unique_name_target' ? { suggestedTargetEntityId: publicEntityIds[0] } : {}) });
  }
  return groups.sort((a, b) => a.token.localeCompare(b.token));
}
```

Implementation requirements:

- Sort IDs, names, sources, and groups before hashing or returning.
- Include public revision plus every involved `submissionRevision` in the SHA-256 token.
- Return `unique_name_target` only when exactly one live public ID exists and at least one non-target pending source is present.
- Return `public_merge` for two or more live public IDs and `ambiguous` when there is no live target.
- Keep source names/game IDs intact for the admin preview; never decide from browser-provided names.

- [ ] **Step 4: Run the focused test and verify GREEN**

```powershell
npm test -- --run tests/library-conflict-resolution.test.ts
```

Expected: all grouping tests PASS.

- [ ] **Step 5: Commit the pure model**

```powershell
git add -- cloud-match-server/src/library-conflict-resolution.ts cloud-match-server/tests/library-conflict-resolution.test.ts
git commit -m "新增共享库冲突分组模型"
```

## Task 2: Atomic Batch Resolution and Durable Redirects

**Files:**
- Modify: `cloud-match-server/src/db.ts:146-200`
- Modify: `cloud-match-server/src/library-admin-data.ts:140-381`
- Create: `cloud-match-server/tests/library-batch-conflict-resolution.test.ts`

- [ ] **Step 1: Write failing database integration tests**

Use an in-memory `openDatabase`, real pending rows, and exported data-layer functions. Cover two submissions resolving into one public target, a mixed submission retaining its unselected row, a public-public merge, and one revision increment:

```ts
import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';
import { createCloudMatchAdminApp } from '../src/admin.js';
import { openDatabase } from '../src/db.js';
import { listPlayerLibrary } from '../src/library-store.js';
import { mutatePublicLibrary, readAdminConflictResolutionState, resolveAdminConflictGroups } from '../src/library-admin-data.js';

const databases: ReturnType<typeof openDatabase>[] = [];
const entity = (entityId: string, names: string[], gameIds: string[] = []) => ({ entityId, names, gameIds });
const pending = (db: ReturnType<typeof openDatabase>, entities: ReturnType<typeof entity>[]) =>
  Number(db.prepare(`INSERT INTO player_library_submissions(device_id,payload_json,status,created_at)
    VALUES('fixture',?,'pending',99)`).run(JSON.stringify({ entities })).lastInsertRowid);
const status = (db: ReturnType<typeof openDatabase>, id: number) =>
  (db.prepare('SELECT status FROM player_library_submissions WHERE id=?').get(id) as { status: string }).status;
afterEach(() => databases.splice(0).forEach(db => db.close()));

test('confirms many unique-name sources into one public target and retires every source ID', () => {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['丁真'], ['old'])]);
  const first = pending(db, [entity('local-a', ['丁真', '别名甲'], ['game-a'])]);
  const second = pending(db, [entity('local-b', ['丁真', '别名乙'], ['game-b'])]);
  const state = readAdminConflictResolutionState(db);
  const group = state.groups.find(item => item.kind === 'unique_name_target')!;

  const result = resolveAdminConflictGroups(db, state.revision, 200,
    [{ token: group.token, targetEntityId: 'public-a' }]);

  expect(result.revision).toBe(2);
  expect(listPlayerLibrary(db).entities[0]).toEqual(entity('public-a',
    ['丁真', '别名甲', '别名乙'], ['old', 'game-a', 'game-b']));
  expect(listPlayerLibrary(db).entityRedirects).toEqual([
    { fromEntityId: 'local-a', toEntityId: 'public-a' },
    { fromEntityId: 'local-b', toEntityId: 'public-a' },
  ]);
  expect(status(db, first)).toBe('approved');
  expect(status(db, second)).toBe('approved');
});

test('keeps unselected entities pending and preserves the byte-exact original submission', () => {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['A'])]);
  const payload = JSON.stringify({ entities: [entity('local-a', ['A'], ['accepted']), entity('clean', ['Clean'], ['later'])] }, null, 2);
  const id = Number(db.prepare(`INSERT INTO player_library_submissions(device_id,payload_json,status,created_at)
    VALUES('fixture',?,'pending',99)`).run(payload).lastInsertRowid);
  const state = readAdminConflictResolutionState(db);
  const group = state.groups.find(item => item.kind === 'unique_name_target')!;
  resolveAdminConflictGroups(db, state.revision, 200, [{ token: group.token, targetEntityId: 'public-a' }]);
  const row = db.prepare('SELECT status,payload_json FROM player_library_submissions WHERE id=?').get(id) as { status: string; payload_json: string };
  expect(row.status).toBe('pending');
  expect(JSON.parse(row.payload_json).entities).toEqual([entity('clean', ['Clean'], ['later'])]);
  expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(id))
    .toEqual({ payload_json: payload });
});

test('merges selected live public owners and publishes the entire batch once', () => {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'import', [entity('public-a', ['A']), entity('public-b', ['B'])]);
  pending(db, [entity('public-a', ['A', 'B'], ['new-game'])]);
  const state = readAdminConflictResolutionState(db);
  const group = state.groups.find(item => item.kind === 'public_merge')!;
  const result = resolveAdminConflictGroups(db, state.revision, 200, [{ token: group.token, targetEntityId: 'public-b' }]);
  expect(result.revision).toBe(state.revision + 1);
  expect(listPlayerLibrary(db).entities).toEqual([entity('public-b', ['A', 'B'], ['new-game'])]);
  expect(listPlayerLibrary(db).entityRedirects).toContainEqual({ fromEntityId: 'public-a', toEntityId: 'public-b' });
});
```

- [ ] **Step 2: Add stale and rollback tests, then verify RED**

Add tests that mutate one pending payload after reading the group and that install a failing audit trigger:

```ts
test('rejects a stale group without changing public or pending data', () => {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['A'])]);
  const id = pending(db, [entity('local-a', ['A'])]);
  const state = readAdminConflictResolutionState(db); const before = listPlayerLibrary(db);
  db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE id=?')
    .run(JSON.stringify({ entities: [entity('local-a', ['Changed'])] }), id);
  expect(() => resolveAdminConflictGroups(db, state.revision, 200,
    [{ token: state.groups[0].token, targetEntityId: 'public-a' }]))
    .toThrowError(expect.objectContaining({ code: 'stale_conflict_group' }));
  expect(listPlayerLibrary(db)).toEqual(before);
});

test('rolls back publication, redirects, submission states and revision when audit insertion fails', () => {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['A'])]);
  pending(db, [entity('local-a', ['A'], ['new-game'])]);
  db.exec(`CREATE TRIGGER fail_conflict_audit BEFORE INSERT ON player_library_conflict_resolution_audit
    BEGIN SELECT RAISE(ABORT,'fixture failure'); END`);
  const beforeLibrary = listPlayerLibrary(db);
  const beforeSubmission = db.prepare('SELECT * FROM player_library_submissions ORDER BY id').all();
  const state = readAdminConflictResolutionState(db);
  expect(() => resolveAdminConflictGroups(db, state.revision, 200,
    [{ token: state.groups[0].token, targetEntityId: state.groups[0].suggestedTargetEntityId! }])).toThrow();
  expect(listPlayerLibrary(db)).toEqual(beforeLibrary);
  expect(db.prepare('SELECT * FROM player_library_submissions ORDER BY id').all()).toEqual(beforeSubmission);
  expect(db.prepare('SELECT * FROM player_library_submission_originals').all()).toEqual([]);
});
```

Run:

```powershell
npm test -- --run tests/library-batch-conflict-resolution.test.ts
```

Expected: FAIL because the audit table and data-layer functions do not exist.

- [ ] **Step 3: Add the append-only audit table**

Add this table to `openDatabase` initialization:

```sql
CREATE TABLE IF NOT EXISTS player_library_conflict_resolution_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  revision INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  groups_json TEXT NOT NULL,
  before_entities_json TEXT NOT NULL,
  after_entities_json TEXT NOT NULL,
  before_redirects_json TEXT NOT NULL,
  after_redirects_json TEXT NOT NULL,
  submissions_json TEXT NOT NULL
);
```

- [ ] **Step 4: Build authoritative global conflict state**

In `library-admin-data.ts`, import the pure planner and add:

```ts
export function buildAdminConflictResolutionState(db: Database.Database,
  current: ReturnType<typeof listPlayerLibrary>,
  allSubmissions = listAdminSubmissions(db, current.entities)) {
  const submissions = allSubmissions.filter(item => item.valid);
  const evidence = readAutomaticIdentityEvidence(db);
  const conflicts = projectedLibrary(current.entities,
    submissions.flatMap(item => item.entities), evidence).conflicts;
  return {
    revision: current.revision,
    submissions,
    groups: buildConflictResolutionGroups({
      revision: current.revision,
      publicEntities: current.entities,
      submissions: submissions.map(({ id, submissionRevision, entities }) => ({ id, submissionRevision, entities })),
      conflicts,
    }),
  };
}
export function readAdminConflictResolutionState(db: Database.Database) {
  const current = listPlayerLibrary(db);
  return buildAdminConflictResolutionState(db, current);
}
```

The final implementation must share the already loaded library/submission objects when called from `/state`, avoiding a second database scan.

- [ ] **Step 5: Generalize manual publication for multiple targets**

Replace `manualTargetId?: string` in `publishEntities` with `manualTargetIds?: ReadonlySet<string>`:

```ts
const normalized = normalizePublicEntities(previous, entities, beforeEvidence, manualTargetIds === undefined);
for (const target of manualTargetIds ?? []) normalized.evidence.delete(target);
```

Update `mergePublicLibrary` to pass `new Set([targetEntityId])`. Existing automatic publication callers pass `undefined` and retain their current behavior.

- [ ] **Step 6: Implement one-transaction resolution**

Add:

```ts
export interface ConflictResolutionDecision { token: string; targetEntityId: string }
export function resolveAdminConflictGroups(db: Database.Database, revision: number, now: number,
  decisions: ConflictResolutionDecision[]) {
  return db.transaction(() => {
    const current = requireLibraryRevision(db, revision);
    const state = buildAdminConflictResolutionState(db, current);
    if (!decisions.length || new Set(decisions.map(item => item.token)).size !== decisions.length) {
      throw new LibraryAdminError(400, 'invalid_request');
    }
    const available = new Map(state.groups.map(group => [group.token, group]));
    const selected = decisions.map(decision => {
      const group = available.get(decision.token);
      if (!group) throw new LibraryAdminError(409, 'stale_conflict_group');
      const validTarget = group.kind === 'unique_name_target' ?
        decision.targetEntityId === group.suggestedTargetEntityId :
        group.kind === 'public_merge' && group.publicEntityIds.includes(decision.targetEntityId);
      if (!validTarget) throw new LibraryAdminError(400, 'invalid_conflict_target');
      return { group, targetEntityId: decision.targetEntityId };
    });
    const publicIds = new Set(current.entities.map(entity => entity.entityId));
    const targetById = new Map<string, string>();
    const selectedSourceKeys = new Set<string>();
    const manualTargets = new Set<string>();
    for (const { group, targetEntityId } of selected) {
      manualTargets.add(targetEntityId);
      for (const id of [...group.publicEntityIds, ...group.sources.map(source => source.entityId)]) {
        const previous = targetById.get(id);
        if (previous && previous !== targetEntityId) throw new LibraryAdminError(409, 'conflicting_resolution');
        targetById.set(id, targetEntityId);
      }
      for (const source of group.sources) selectedSourceKeys.add(`${source.submissionId}\0${source.entityId}`);
    }
    const next = new Map(current.entities.map(entity => [entity.entityId, structuredClone(entity)]));
    const append = (targetEntityId: string, source: PlayerEntity) => {
      const target = next.get(targetEntityId);
      if (!target) throw new LibraryAdminError(404, 'entity_not_found');
      next.set(targetEntityId, additiveEntities([target], [{ ...source, entityId: targetEntityId }])[0]);
    };
    for (const entity of current.entities) {
      const target = targetById.get(entity.entityId);
      if (target && target !== entity.entityId) { append(target, entity); next.delete(entity.entityId); }
    }
    for (const { group, targetEntityId } of selected) for (const source of group.sources) append(targetEntityId, source);
    const projected = [...next.values()];
    const redirects = canonicalEntityRedirects(projected, [
      ...current.entityRedirects.map(item => ({ ...item, toEntityId: targetById.get(item.toEntityId) ?? item.toEntityId })),
      ...[...targetById].filter(([from, to]) => from !== to)
        .map(([fromEntityId, toEntityId]) => ({ fromEntityId, toEntityId })),
    ]);
    const beforeSubmissions = state.submissions.map(item => ({ id: item.id, submissionRevision: item.submissionRevision }));
    const nextRevision = publishEntities(db, projected, current.entities, now, redirects, manualTargets);
    let associatedEntityCount = 0;
    for (const submission of state.submissions) {
      const accepted = submission.entityTargets.map((entityId, index) =>
        selectedSourceKeys.has(`${submission.id}\0${entityId}`) ? index : -1).filter(index => index >= 0);
      if (!accepted.length) continue;
      const row = db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(submission.id) as { payload_json: string };
      const raw = boundedJson(row.payload_json) as { entities: PlayerEntity[] };
      db.prepare(`INSERT OR IGNORE INTO player_library_submission_originals(submission_id,payload_json)
        VALUES(?,?)`).run(submission.id, row.payload_json);
      const acceptedIndexes = new Set(accepted); associatedEntityCount += accepted.length;
      const remaining = raw.entities.filter((_entity, index) => !acceptedIndexes.has(index));
      db.prepare(`UPDATE player_library_submissions SET payload_json=?,status=?,reviewed_at=?,review_reason=? WHERE id=?`)
        .run(remaining.length ? JSON.stringify({ entities: remaining }) : row.payload_json,
          remaining.length ? 'pending' : 'approved', now,
          remaining.length ? 'admin_conflicts_partially_resolved' : 'admin_conflicts_resolved', submission.id);
    }
    const published = listPlayerLibrary(db);
    db.prepare(`INSERT INTO player_library_conflict_resolution_audit
      (revision,created_at,groups_json,before_entities_json,after_entities_json,before_redirects_json,after_redirects_json,submissions_json)
      VALUES(?,?,?,?,?,?,?,?)`).run(nextRevision, now, JSON.stringify(selected), JSON.stringify(current.entities),
        JSON.stringify(published.entities), JSON.stringify(current.entityRedirects), JSON.stringify(published.entityRedirects),
        JSON.stringify(beforeSubmissions));
    return { revision: nextRevision, resolvedGroupCount: selected.length, associatedEntityCount,
      mergedPublicEntityCount: [...targetById].filter(([from, to]) => from !== to && publicIds.has(from)).length,
      redirectCount: published.entityRedirects.length - current.entityRedirects.length,
      pendingSubmissionCount: listAdminSubmissions(db, published.entities).length };
  }).immediate();
}
```

Required transaction rules:

- Reject duplicate tokens, empty decisions, missing groups, mismatched unique targets, and public-merge targets outside their component.
- Map every non-target component ID to the selected target; do not map an ID used by an unselected component.
- Preserve `player_library_submission_originals` before removing accepted rows from a pending payload.
- Mark a submission `approved` only when no rows remain; otherwise keep it `pending` with `admin_conflicts_partially_resolved`.
- Retarget existing redirect destinations before deleting a live source, then add canonical redirects for every retired/pending source.
- Validate the complete final projection through `publishEntities`; do not write names incrementally before validation.
- Insert the batch audit after publication and submission rewrites but before commit so an audit failure rolls everything back.
- Return `resolvedGroupCount`, `associatedEntityCount`, `mergedPublicEntityCount`, `redirectCount`, `pendingSubmissionCount`, and `revision`.

- [ ] **Step 7: Run focused and existing server regressions**

```powershell
npm test -- --run tests/library-batch-conflict-resolution.test.ts tests/library-conflict-resolution.test.ts tests/library-partial-review.test.ts tests/library-shared-identifiers.test.ts
```

Expected: all selected tests PASS, including existing explicit merge and redirect behavior.

- [ ] **Step 8: Commit the transaction layer**

```powershell
git add -- cloud-match-server/src/db.ts cloud-match-server/src/library-admin-data.ts cloud-match-server/tests/library-batch-conflict-resolution.test.ts
git commit -m "实现共享库冲突批量事务处理"
```

## Task 3: Guarded Admin API

**Files:**
- Modify: `cloud-match-server/src/library-admin.ts:1-120`
- Modify: `cloud-match-server/tests/library-batch-conflict-resolution.test.ts`

- [ ] **Step 1: Add failing HTTP contract tests**

Exercise real Basic auth/CSRF through `createCloudMatchAdminApp`:

```ts
function httpFixture() {
  const db = openDatabase(':memory:'); databases.push(db);
  mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['A'])]);
  pending(db, [entity('local-a', ['A'], ['new-game'])]);
  const app = createCloudMatchAdminApp({ db, now: () => 200, csrfToken: 'csrf', adminPassword: 'password',
    socketController: { getActiveDeviceIds: () => new Set(), disconnectDevice: () => false,
      stopRealtimeViewer: () => false, notifyDirectoryChanged: () => {} } });
  const get = (url: string) => request(app).get('/admin/api/library' + url).auth('admin', 'password');
  const post = (url: string, body: object) => request(app).post('/admin/api/library' + url)
    .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send(body);
  return { app, db, get, post };
}

test('exposes server-computed groups and resolves selected tokens through one guarded endpoint', async () => {
  const { get, post } = httpFixture();
  const state = await get('/state').expect(200);
  const group = state.body.conflictResolutionGroups.find((item: { kind: string }) => item.kind === 'unique_name_target');
  const result = await post('/conflicts/resolve', {
    revision: state.body.revision,
    groups: [{ token: group.token, targetEntityId: group.suggestedTargetEntityId }],
    confirm: true,
  }).expect(200);
  expect(result.body).toMatchObject({ ok: true, resolvedGroupCount: 1, revision: state.body.revision + 1 });
});

test('rejects missing confirmation, duplicate tokens, invalid IDs, missing CSRF and stale groups', async () => {
  const { app, db, get, post } = httpFixture();
  const state = await get('/state').expect(200);
  const group = state.body.conflictResolutionGroups[0];
  const decision = { token: group.token, targetEntityId: group.suggestedTargetEntityId };
  await post('/conflicts/resolve', { revision: state.body.revision, groups: [decision] }).expect(400);
  await post('/conflicts/resolve', { revision: state.body.revision, groups: [decision, decision], confirm: true }).expect(400);
  await request(app).post('/admin/api/library/conflicts/resolve').auth('admin', 'password')
    .send({ revision: state.body.revision, groups: [decision], confirm: true }).expect(403);
  db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE status=\'pending\'')
    .run(JSON.stringify({ entities: [entity('changed', ['Changed'])] }));
  await post('/conflicts/resolve', { revision: state.body.revision, groups: [decision], confirm: true }).expect(409);
  expect(listPlayerLibrary(db).revision).toBe(state.body.revision);
});
```

- [ ] **Step 2: Run the HTTP test and verify RED**

```powershell
npm test -- --run tests/library-batch-conflict-resolution.test.ts
```

Expected: FAIL with `conflictResolutionGroups` missing and `/conflicts/resolve` returning 404.

- [ ] **Step 3: Add Zod schemas and route wiring**

In `library-admin.ts`:

```ts
const conflictDecision = z.object({
  token: z.string().regex(/^[a-f0-9]{64}$/),
  targetEntityId: entityIdSchema,
}).strict();
const resolveConflictsBody = guarded.extend({
  groups: z.array(conflictDecision).min(1).max(100),
  confirm: z.literal(true),
}).refine(body => new Set(body.groups.map(group => group.token)).size === body.groups.length);

router.post('/conflicts/resolve', (request, response) => {
  const body = parse(resolveConflictsBody, request.body);
  response.json({ ok: true, ...resolveAdminConflictGroups(db, body.revision, now(), body.groups) });
});
```

Extend `/state` with all unfiltered `conflictResolutionGroups` and summary counts. Search/filter query parameters may filter the submission list but must not silently remove batch candidates.

Build them from the same transaction snapshot without re-reading rows:

```ts
const conflictState = buildAdminConflictResolutionState(db, library, submissions);
return {
  revision: library.revision,
  conflictResolutionGroups: conflictState.groups,
  conflictResolutionStats: {
    uniqueNameTargets: conflictState.groups.filter(group => group.kind === 'unique_name_target').length,
    publicMerges: conflictState.groups.filter(group => group.kind === 'public_merge').length,
    ambiguous: conflictState.groups.filter(group => group.kind === 'ambiguous').length,
  },
  entities: library.entities.filter(entity => matches(entity, q)),
  submissions: submissions.filter(item =>
    (!pendingQ || item.deviceId.toLocaleLowerCase().includes(pendingQ) || item.entities.some(entity => matches(entity, pendingQ))) &&
    (filter === 'conflict' ? !item.valid || item.conflicts.length > 0 :
      filter === 'clean' ? item.valid && !item.conflicts.length : true)),
};
```

- [ ] **Step 4: Add the stale-group message code and verify GREEN**

Return `LibraryAdminError(409, 'stale_conflict_group')` for token/target mismatches. Run:

```powershell
npm test -- --run tests/library-batch-conflict-resolution.test.ts tests/library-admin.test.ts tests/library-partial-review.test.ts
npm run typecheck:test
```

Expected: all tests and test typechecking PASS.

- [ ] **Step 5: Commit the API contract**

```powershell
git add -- cloud-match-server/src/library-admin.ts cloud-match-server/tests/library-batch-conflict-resolution.test.ts
git commit -m "开放共享库冲突批量确认接口"
```

## Task 4: Admin Batch Preview and One-Click Name Merge

**Files:**
- Modify: `cloud-match-server/src/library-admin-page.ts:1-542`
- Create: `scripts/check-cloud-library-conflict-batch.cjs`

- [ ] **Step 1: Write the failing real-browser check**

Create an isolated SQLite fixture with two unique-target groups, one two-public group, and one ambiguous group. Assert these behaviors:

```js
await page.goto(f.url + '/admin/library');
await idle();
assert.equal(await page.locator('#btn-resolve-by-name').isDisabled(), false);
await page.locator('#btn-resolve-by-name').click();
assert.match(await page.locator('#conflict-batch-summary').innerText(), /按名称可合并 2/);
assert.match(await page.locator('#conflict-batch-summary').innerText(), /已跳过 2/);
assert.equal(await page.locator('[data-resolution-kind="unique_name_target"] input:checked').count(), 2);
assert.equal(await page.locator('[data-resolution-kind="ambiguous"] input:enabled').count(), 0);

const request = page.waitForRequest(req => req.url().endsWith('/conflicts/resolve'));
await page.locator('#btn-conflict-batch-confirm').click();
assert.equal((await request).postDataJSON().groups.length, 2);
```

Also dismiss the confirmation once and assert the database is unchanged; then accept it and verify names, game IDs, redirects, pending counts, and no page errors. Capture open-dialog screenshots at `1366x900` and `390x844`, checking horizontal overflow and visible controls.

- [ ] **Step 2: Build the server and verify browser RED**

```powershell
npm run build
node ..\scripts\check-cloud-library-conflict-batch.cjs
```

Expected: FAIL because `#btn-resolve-by-name` and the dialog do not exist.

- [ ] **Step 3: Add semantic dialog markup and restrained responsive CSS**

Add a footer command and a real dialog, not nested cards:

```html
<button id="btn-resolve-by-name" class="primary">按名称一键合并</button>
<dialog id="conflict-batch-dialog" aria-labelledby="conflict-batch-heading">
  <h2 id="conflict-batch-heading">冲突批量处理</h2>
  <p id="conflict-batch-summary"></p>
  <div id="conflict-batch-list"></div>
  <div class="review-actions">
    <button id="btn-conflict-batch-cancel">取消</button>
    <button id="btn-conflict-batch-confirm" class="danger">确认所选合并</button>
  </div>
</dialog>
```

Use bounded dialog dimensions, stable checkbox/select tracks, wrapping names/IDs, visible focus, and the existing palette. At 390 px, controls must stack without horizontal scrolling.

- [ ] **Step 4: Render only server-authorized choices**

Extend the existing page state and controls:

```js
function oneClickGroups() {
  return (state.conflictResolutionGroups || []).filter(group => group.kind === 'unique_name_target');
}
function resolutionChoices() {
  return [...document.querySelectorAll('[data-resolution-token]')]
    .filter(row => row.querySelector('input[type="checkbox"]')?.checked)
    .map(row => ({ token: row.dataset.resolutionToken, targetEntityId: row.querySelector('select').value }));
}
```

Clicking “按名称一键合并” must:

- Preselect every and only `unique_name_target` group.
- Render names, source IDs, submission counts, target names/ID, and field-union counts with `textContent`.
- Show multi-public groups unchecked with a required target selector.
- Show ambiguous groups disabled with a reason.
- Display included and skipped counts before the final `confirm()`.
- POST the exact selected tokens and targets to `/conflicts/resolve`, close on success, refresh, and retain the dialog on errors.
- Treat an open conflict batch dialog like the existing merge dialog: pause silent auto-refresh, preserve its selections, and resume refresh after close.

Add `stale_conflict_group: '冲突数据已变化，请刷新后重新确认。'` to the existing error map.

- [ ] **Step 5: Run browser and server UI regressions**

```powershell
npm run build
node ..\scripts\check-cloud-library-conflict-batch.cjs
node ..\scripts\check-cloud-library-admin.cjs
```

Expected: both real Edge scripts PASS with no page errors and responsive screenshots generated under `build/`.

- [ ] **Step 6: Commit the admin experience**

```powershell
git add -- cloud-match-server/src/library-admin-page.ts scripts/check-cloud-library-conflict-batch.cjs
git commit -m "增加按名称一键合并后台"
```

## Task 5: Client Conflict Reporting After Partial Pull

**Files:**
- Modify: `scripts/player_library_push_integration_test.js:28-90`
- Modify: `DNFGameCaptureDlg.cpp:15320-15608`

- [ ] **Step 1: Change the static orchestration test to the desired behavior**

Replace the 5.2.1 pause assertion with explicit order and checkpoint expectations:

```js
test('partial public pulls report the persisted conflicting snapshot without completing the cycle', () => {
  const imported = autoResult.indexOf('QueuePlayerLibraryImport(pending->publicAliasDbJson');
  const partial = autoResult.indexOf('pending->libraryPartial', imported);
  const rebuilt = autoResult.indexOf('librarySnapshot->v2Entities', partial);
  const submitted = autoResult.indexOf('DnfSubmitV2PlayerLibrary(', rebuilt);
  assert(imported >= 0 && partial > imported && rebuilt > partial && submitted > rebuilt);
  assert.doesNotMatch(autoResult, /公共库存在归属冲突，已暂停自动投稿/);
  assert.match(autoResult, /冲突资料已送后台，等待管理员批量确认/);
  assert.match(autoResult, /pending->libraryPartial \|\|\s*!m_playerLibraryPushTracker\.ShouldSkip/);
  assert.match(autoResult, /const bool cycleReady = !result->libraryPartial/);
});
```

Retain the existing tests that require the post-import lifecycle/scope checks to occur before reading `v2Entities`.

- [ ] **Step 2: Run the client orchestration test and verify RED**

```powershell
node --test scripts/player_library_push_integration_test.js
```

Expected: FAIL because the partial branch still sets “已暂停自动投稿” and bypasses submission.

- [ ] **Step 3: Allow the existing post-import submit path for partial imports**

In `OnAliasDbAutoSyncResult`:

- Remove the early `if (pending->libraryPartial)` push suppression.
- Keep the committed-snapshot, generation, lifetime, authorization-scope, endpoint, and persisted checks unchanged.
- Rebuild `submitRequest` from `librarySnapshot->v2Entities` for both complete and partial imports.
- Bypass the local acknowledged-signature shortcut while `libraryPartial` is true. A previously acknowledged unchanged payload may become conflicting after the public revision changes; the server submission signature remains the authoritative duplicate guard.
- After an acknowledged partial submission, map `PendingReview` to “冲突资料已送后台，等待管理员批量确认” and `AlreadyPending` to “冲突资料已在后台，等待管理员批量确认”.
- Preserve the actual failure message when the POST fails.

Use this status shape when the continuation returns:

```cpp
const bool shouldSubmit = pending->libraryPartial ||
    !m_playerLibraryPushTracker.ShouldSkip(pending->submittedSignature);
if (continued->libraryPartial && continued->pushOk) {
    continued->pushMessage = continued->pushStatus ==
        dnf::player_library_sync::SubmissionStatus::AlreadyPending ?
        "冲突资料已在后台，等待管理员批量确认" :
        "冲突资料已送后台，等待管理员批量确认";
}
```

In the final UI-state branch, a partial upload failure must be `failed`; an acknowledged pending submission may be `success`, while `AlreadyPending` remains `skipped`. Keep `cycleReady = !result->libraryPartial && ...` so the seven-day checkpoint is not advanced.

- [ ] **Step 4: Run focused client regressions**

```powershell
node --test scripts/player_library_push_integration_test.js scripts/alias_db_auto_sync_feature_test.js scripts/match_library_bridge_test.js
```

Expected: all tests PASS; the static ordering assertion confirms pull commit precedes payload rebuild and POST.

- [ ] **Step 5: Commit the client behavior**

```powershell
git add -- DNFGameCaptureDlg.cpp scripts/player_library_push_integration_test.js
git commit -m "让客户端上报真实选手库冲突"
```

## Task 6: 5.2.2 Release Metadata and Packages

**Files:**
- Modify: `DNFGameCaptureDlg.h:56`
- Modify: `DNFGameCapture.rc:88,139-162`
- Modify: `scripts/DNFGameCapture-5.0.2.iss:2`
- Modify: `web前端/index.html:7-8,518`
- Modify: `web前端/main.js:87`
- Modify: `scripts/check-broadcaster-preview-layout-feature.ps1`
- Modify: `scripts/check-cloud-authorization-gate-feature.ps1`
- Modify: `scripts/check-cloud-endpoint-cache.ps1`
- Modify: `scripts/check-player-identity-group.ps1`
- Modify: `scripts/player_library_web_test.js`
- Modify: `scripts/production_release_integration_test.js`
- Modify: `scripts/package-production-client.ps1`
- Modify: `cloud-match-server/tests/production-package.test.ts`
- Create: `cloud-match-server/README-production-5.2.2.md`
- Create: `scripts/package-production-5.2.2.ps1`
- Create: `docs/release-5.2.2.md`

- [ ] **Step 1: Update release tests first and verify RED**

Change expected metadata to:

```text
CURRENT_VERSION L"5.2.2"
FILEVERSION 5,2,2,0
PRODUCTVERSION 5,2,2,0
WEB_LAYOUT_VERSION = '20260912-5.2.2-production'
update_v522.zip
dnf-cloud-match-server-production-5.2.2.zip
```

Add production package assertions for a clean TypeScript build, `library-conflict-resolution.js`, credential-free allowlist, SHA256 manifest, and `serverFirstRequired=true`.

Run:

```powershell
node --test scripts/production_release_integration_test.js scripts/player_library_web_test.js
npm test -- --run tests/production-package.test.ts
```

Expected: FAIL on the still-current 5.2.1 markers and missing 5.2.2 server package script.

- [ ] **Step 2: Bump native, installer, frontend, and client package metadata**

Set all listed files to 5.2.2/`20260912-5.2.2-production`. Change the default client archive to `deployment-packages/update_v522.zip` and require EXE version `5.2.2.0`. Do not alter unrelated package dependencies containing version `5.2.1`.

- [ ] **Step 3: Add the server package and deployment guide**

Copy the established production packaging policy into `package-production-5.2.2.ps1`, changing only release identity, README source, required runtime modules, and `release.json` client version. The package must:

- Compile TypeScript into a private temporary directory.
- Require `library-conflict-resolution.js` and all existing runtime entry points.
- Include only JS runtime, package manifests, approved deployment files, checksums, and documentation.
- Refuse to overwrite an existing archive.
- Never include database files, environment secrets, passwords, logs, test data, or source maps.
- Validate every ZIP entry hash before moving the archive to the requested output directory.

Document server-first backup, stop/install/preflight/start/health/admin verification, rollback, and the fact that no real conflict batch is executed automatically.

- [ ] **Step 4: Add release notes and rerun metadata checks**

`docs/release-5.2.2.md` must describe the source fix, partial conflict reporting, batch name confirmation safety boundary, redirects, server-first order, local artifact hashes (filled only after packaging), and the prohibition on automatic production deployment.

Run:

```powershell
node --test scripts/production_release_integration_test.js scripts/player_library_web_test.js scripts/player_library_push_integration_test.js
npm test -- --run tests/production-package.test.ts
```

Expected: PASS.

- [ ] **Step 5: Commit release wiring**

```powershell
git add -- DNFGameCaptureDlg.h DNFGameCapture.rc scripts/DNFGameCapture-5.0.2.iss web前端/index.html web前端/main.js scripts/check-broadcaster-preview-layout-feature.ps1 scripts/check-cloud-authorization-gate-feature.ps1 scripts/check-cloud-endpoint-cache.ps1 scripts/check-player-identity-group.ps1 scripts/player_library_web_test.js scripts/production_release_integration_test.js scripts/package-production-client.ps1 cloud-match-server/tests/production-package.test.ts cloud-match-server/README-production-5.2.2.md scripts/package-production-5.2.2.ps1 docs/release-5.2.2.md
git commit -m "准备 5.2.2 共享库冲突处理版本"
```

## Task 7: Full Verification and Local Artifacts

**Files:**
- Modify: `docs/release-5.2.2.md` only to insert measured hashes/results
- Generated, untracked: isolated build output and release archives under `build/` or `deployment-packages/`

- [ ] **Step 1: Run all server checks**

From `cloud-match-server`:

```powershell
npm run build
npm run typecheck:test
npm test -- --run
```

Expected: TypeScript build/typecheck exit 0 and every Vitest file passes.

- [ ] **Step 2: Run browser checks**

```powershell
node scripts/check-cloud-library-conflict-batch.cjs
node scripts/check-cloud-library-admin.cjs
node scripts/check-cloud-library-review.cjs
```

Expected: all scripts PASS; Edge reports no page errors; desktop/mobile screenshots have no overflow or overlapping controls.

- [ ] **Step 3: Run focused native/static regressions**

```powershell
node --test scripts/player_library_push_integration_test.js scripts/alias_db_auto_sync_feature_test.js scripts/match_library_bridge_test.js scripts/production_release_integration_test.js scripts/player_library_web_test.js
powershell -ExecutionPolicy Bypass -File scripts/check-player-library-core.ps1
powershell -ExecutionPolicy Bypass -File scripts/check-cloud-authorization-gate-feature.ps1
```

Expected: all Node tests and PowerShell/native checks exit 0.

- [ ] **Step 4: Build Release x64 without touching the running 5.2.1 EXE**

Use the installed Visual Studio MSBuild with dedicated absolute output/intermediate directories:

```powershell
& 'E:\VS2026\MSBuild\Current\Bin\amd64\MSBuild.exe' '..\DNFGameCapture.slnx' /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:OutDir='C:\Users\BRO\source\repos\DNFGameCapture\build\client-5.2.2\' /p:IntDir='C:\Users\BRO\source\repos\DNFGameCapture\build\obj-5.2.2\' /m
```

Expected: exit 0, zero compiler/linker errors, and `build/client-5.2.2/DNFGameCapture.exe` reports `5.2.2.0`. Do not terminate PID 24028 or overwrite the parent `x64/Release` directory without explicit user approval.

- [ ] **Step 5: Validate and create local packages**

Create a private runtime staging directory from the existing approved runtime files, replace only the EXE and ten frontend source files, then run:

```powershell
$stage = 'C:\Users\BRO\source\repos\DNFGameCapture\build\runtime-5.2.2'
if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage" }
New-Item -ItemType Directory -Path (Join-Path $stage 'web前端') | Out-Null
$runtime = 'C:\Users\BRO\source\repos\DNFGameCapture\x64\Release'
foreach ($name in @('WebView2Loader.dll','7za.exe','sprite(击杀大XX).NPK')) {
  Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $stage
}
Copy-Item -LiteralPath 'C:\Users\BRO\source\repos\DNFGameCapture\build\client-5.2.2\DNFGameCapture.exe' -Destination $stage
foreach ($name in @('autocomplete-worker.js','index.html','main.js','style.css','keys.css','keys.html','keys.js','kill.css','kill.html','kill.js')) {
  Copy-Item -LiteralPath (Join-Path 'C:\Users\BRO\source\repos\DNFGameCapture\DNFGameCapture\web前端' $name) -Destination (Join-Path $stage 'web前端')
}
powershell -ExecutionPolicy Bypass -File scripts/package-production-5.2.2.ps1 -ValidateOnly
powershell -ExecutionPolicy Bypass -File scripts/package-production-5.2.2.ps1
powershell -ExecutionPolicy Bypass -File scripts/package-production-client.ps1 -ReleaseDir 'C:\Users\BRO\source\repos\DNFGameCapture\build\runtime-5.2.2'
```

Expected: one server ZIP plus SHA256 sidecar and `update_v522.zip`, with no user databases/configuration/licenses/logs. This step creates local artifacts only; it does not upload OSS or deploy the server.

- [ ] **Step 6: Record measured evidence and run final hygiene checks**

Insert actual test totals, EXE/package SHA-256 values, and artifact paths into `docs/release-5.2.2.md`, then run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; only intended tracked release-note edits plus pre-existing unrelated untracked files.

- [ ] **Step 7: Commit verification evidence**

```powershell
git add -- docs/release-5.2.2.md
git commit -m "记录 5.2.2 验证与产物"
```

- [ ] **Step 8: Review and push only after final verification**

Inspect the branch diff against `396b5a9`, confirm no local data/artifacts are tracked, and push the feature branch only if all required commands above passed. Production deployment, OSS upload, update-manifest publication, live batch confirmation, and replacing the currently running client remain separate explicit user-authorized actions.
