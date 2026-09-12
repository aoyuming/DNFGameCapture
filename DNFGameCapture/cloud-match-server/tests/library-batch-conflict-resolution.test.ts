import request from 'supertest';
import { afterEach, describe, expect, test } from 'vitest';
import { createCloudMatchAdminApp } from '../src/admin.js';
import { openDatabase } from '../src/db.js';
import {
  buildAdminConflictResolutionState,
  listAdminSubmissions,
  mutatePublicLibrary,
  readAdminConflictResolutionState,
  resolveAdminConflictGroups,
  type ConflictResolutionDecision,
} from '../src/library-admin-data.js';
import { listPlayerLibrary, MAX_ENTITY_REDIRECTS } from '../src/library-store.js';
import type { PlayerEntity } from '../src/player-library.js';

const databases: ReturnType<typeof openDatabase>[] = [];
const entity = (entityId: string, names: string[] = [entityId], gameIds: string[] = []): PlayerEntity =>
  ({ entityId, names, gameIds });

function open() {
  const db = openDatabase(':memory:');
  databases.push(db);
  return db;
}

function httpFixture() {
  const db = open();
  const app = createCloudMatchAdminApp({
    db,
    now: () => 200,
    csrfToken: 'csrf',
    adminPassword: 'password',
    socketController: {
      getActiveDeviceIds: () => new Set(),
      disconnectDevice: () => false,
      stopRealtimeViewer: () => false,
      notifyDirectoryChanged: () => {},
    },
  });
  const getState = (query = '') => request(app).get('/admin/api/library/state' + query).auth('admin', 'password');
  const postResolve = (body: object) => request(app).post('/admin/api/library/conflicts/resolve')
    .auth('admin', 'password').set('x-dnf-admin-csrf', 'csrf').send(body);
  return { app, db, getState, postResolve };
}

function insertPending(db: ReturnType<typeof openDatabase>, entities: unknown[], formatted = false) {
  const payload = JSON.stringify({ entities }, null, formatted ? 2 : undefined);
  const id = Number(db.prepare(`INSERT INTO player_library_submissions(device_id,payload_json,status,created_at)
    VALUES('fixture',?,'pending',99)`).run(payload).lastInsertRowid);
  return { id, payload };
}

function submissionRow(db: ReturnType<typeof openDatabase>, id: number) {
  return db.prepare(`SELECT id,payload_json,status,reviewed_at,review_reason
    FROM player_library_submissions WHERE id=?`).get(id) as {
      id: number;
      payload_json: string;
      status: string;
      reviewed_at: number | null;
      review_reason: string | null;
    };
}

function mutationSnapshot(db: ReturnType<typeof openDatabase>) {
  return {
    library: listPlayerLibrary(db),
    submissions: db.prepare('SELECT * FROM player_library_submissions ORDER BY id').all(),
    originals: db.prepare('SELECT * FROM player_library_submission_originals ORDER BY submission_id').all(),
    audits: db.prepare('SELECT * FROM player_library_conflict_resolution_audit ORDER BY id').all(),
  };
}

function expectLibraryAdminError(run: () => unknown, status: number, code: string) {
  expect(run).toThrowError(expect.objectContaining({ status, code }));
}

afterEach(() => databases.splice(0).forEach(db => db.close()));

describe('batch library conflict resolution', () => {
  test('initializes the durable batch audit schema', () => {
    const db = open();

    expect((db.pragma('table_info(player_library_conflict_resolution_audit)') as Array<{ name: string }>)
      .map(column => column.name)).toEqual([
      'id',
      'revision',
      'created_at',
      'groups_json',
      'before_entities_json',
      'after_entities_json',
      'before_redirects_json',
      'after_redirects_json',
      'submissions_json',
    ]);
  });

  test('resolves two same-name submissions into one public target with complete unions and durable audit data', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Shared', 'Public Alias'], ['game-public'])]);
    const first = insertPending(db, [entity('local-a', ['Shared', 'Alias A'], ['game-a'])], true);
    const second = insertPending(db, [entity('local-b', ['Shared', 'Alias B'], ['game-b'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target');

    expect(group).toMatchObject({
      publicEntityIds: ['public-a'],
      suggestedTargetEntityId: 'public-a',
      sources: [{ entityId: 'local-a' }, { entityId: 'local-b' }],
    });
    const result = resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group!.token, targetEntityId: 'public-a' },
    ]);

    expect(result).toEqual({
      revision: 2,
      resolvedGroupCount: 1,
      associatedEntityCount: 2,
      mergedPublicEntityCount: 0,
      redirectCount: 2,
      pendingSubmissionCount: 0,
    });
    const library = listPlayerLibrary(db);
    expect(library.revision).toBe(2);
    expect(library.entities).toHaveLength(1);
    expect(library.entities[0].entityId).toBe('public-a');
    expect(library.entities[0].names).toEqual(expect.arrayContaining(['Shared', 'Public Alias', 'Alias A', 'Alias B']));
    expect(library.entities[0].names).toHaveLength(4);
    expect(library.entities[0].gameIds).toEqual(expect.arrayContaining(['game-public', 'game-a', 'game-b']));
    expect(library.entities[0].gameIds).toHaveLength(3);
    expect(library.entityRedirects).toEqual([
      { fromEntityId: 'local-a', toEntityId: 'public-a' },
      { fromEntityId: 'local-b', toEntityId: 'public-a' },
    ]);
    expect(submissionRow(db, first.id)).toMatchObject({
      payload_json: first.payload,
      status: 'approved',
      reviewed_at: 200,
      review_reason: 'admin_conflicts_resolved',
    });
    expect(submissionRow(db, second.id)).toMatchObject({
      payload_json: second.payload,
      status: 'approved',
      reviewed_at: 200,
      review_reason: 'admin_conflicts_resolved',
    });
    expect(db.prepare('SELECT * FROM player_library_submission_originals ORDER BY submission_id').all()).toEqual([
      { submission_id: first.id, payload_json: first.payload },
      { submission_id: second.id, payload_json: second.payload },
    ]);

    const audit = db.prepare('SELECT * FROM player_library_conflict_resolution_audit').get() as Record<string, unknown> & {
      groups_json: string;
      before_entities_json: string;
      after_entities_json: string;
      before_redirects_json: string;
      after_redirects_json: string;
      submissions_json: string;
    };
    expect(audit).toMatchObject({ revision: 2, created_at: 200 });
    expect(JSON.parse(audit.groups_json)).toMatchObject([{
      targetEntityId: 'public-a',
      group: {
        token: group!.token,
        kind: 'unique_name_target',
        publicEntityIds: ['public-a'],
        sources: [
          { submissionId: first.id, entityId: 'local-a' },
          { submissionId: second.id, entityId: 'local-b' },
        ],
      },
    }]);
    expect(JSON.parse(audit.before_entities_json)).toEqual([
      entity('public-a', ['Public Alias', 'Shared'], ['game-public']),
    ]);
    expect(JSON.parse(audit.after_entities_json)).toEqual(library.entities);
    expect(JSON.parse(audit.before_redirects_json)).toEqual([]);
    expect(JSON.parse(audit.after_redirects_json)).toEqual(library.entityRedirects);
    expect(JSON.parse(audit.submissions_json)).toEqual(expect.arrayContaining([
      { id: first.id, submissionRevision: state.submissions.find(item => item.id === first.id)!.submissionRevision },
      { id: second.id, submissionRevision: state.submissions.find(item => item.id === second.id)!.submissionRevision },
    ]));
  });

  test('removes only selected raw rows from a mixed submission and preserves its byte-exact original', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const selectedWithoutId = { names: ['Alpha', 'Accepted Alias'], gameIds: ['accepted-game'] };
    const clean = entity('clean', ['Clean'], ['later-game']);
    const input = insertPending(db, [selectedWithoutId, clean], true);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;

    expect(group.sources[0].entityId).toBe(`submission-${input.id}-0`);
    const result = resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ]);

    expect(result).toMatchObject({ associatedEntityCount: 1, pendingSubmissionCount: 1 });
    const row = submissionRow(db, input.id);
    expect(row).toMatchObject({
      status: 'pending',
      reviewed_at: 200,
      review_reason: 'admin_conflicts_partially_resolved',
    });
    expect(JSON.parse(row.payload_json)).toEqual({ entities: [clean] });
    expect(db.prepare('SELECT payload_json FROM player_library_submission_originals WHERE submission_id=?').get(input.id))
      .toEqual({ payload_json: input.payload });
    expect(listPlayerLibrary(db).entityRedirects).toEqual([
      { fromEntityId: `submission-${input.id}-0`, toEntityId: 'public-a' },
    ]);
    expect(listPlayerLibrary(db).entities.some(item => item.entityId === 'clean')).toBe(false);
  });

  test('keeps an unselected missing source ID stable when an earlier raw row is removed', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const remainingWithoutId = { names: ['Clean'], gameIds: ['later-game'] };
    const input = insertPending(db, [entity('local-a', ['Alpha']), remainingWithoutId]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;

    resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ]);

    expect(JSON.parse(submissionRow(db, input.id).payload_json)).toEqual({ entities: [{
      ...remainingWithoutId,
      entityId: `submission-${input.id}-1`,
    }] });
  });

  test('merges a public-public conflict into the explicitly retained live target', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [
      entity('public-a', ['Alpha'], ['game-a']),
      entity('public-b', ['Beta'], ['game-b']),
    ]);
    const input = insertPending(db, [entity('public-a', ['Alpha', 'Beta', 'New Alias'], ['game-new'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'public_merge')!;

    expect(group.publicEntityIds).toEqual(['public-a', 'public-b']);
    const result = resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-b' },
    ]);

    expect(result).toMatchObject({ revision: 2, mergedPublicEntityCount: 1, redirectCount: 1 });
    const library = listPlayerLibrary(db);
    expect(library.entities).toHaveLength(1);
    expect(library.entities[0].entityId).toBe('public-b');
    expect(library.entities[0].names).toEqual(expect.arrayContaining(['Alpha', 'Beta', 'New Alias']));
    expect(library.entities[0].names).toHaveLength(3);
    expect(library.entities[0].gameIds).toEqual(expect.arrayContaining(['game-a', 'game-b', 'game-new']));
    expect(library.entities[0].gameIds).toHaveLength(3);
    expect(library.entityRedirects).toEqual([{ fromEntityId: 'public-a', toEntityId: 'public-b' }]);
    expect(submissionRow(db, input.id).status).toBe('approved');
  });

  test('resolves two disconnected selected groups in one publication revision', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('public-a', ['Alpha']), entity('public-b', ['Beta'])]);
    const first = insertPending(db, [entity('local-a', ['Alpha'], ['game-a'])]);
    const second = insertPending(db, [entity('local-b', ['Beta'], ['game-b'])]);
    const state = readAdminConflictResolutionState(db);

    expect(state.groups).toHaveLength(2);
    const decisions = state.groups.map(group => ({
      token: group.token,
      targetEntityId: group.suggestedTargetEntityId!,
    }));
    const result = resolveAdminConflictGroups(db, state.revision, 200, decisions);

    expect(result).toEqual({
      revision: state.revision + 1,
      resolvedGroupCount: 2,
      associatedEntityCount: 2,
      mergedPublicEntityCount: 0,
      redirectCount: 2,
      pendingSubmissionCount: 0,
    });
    expect(listPlayerLibrary(db).revision).toBe(2);
    expect(listPlayerLibrary(db).entityRedirects).toEqual([
      { fromEntityId: 'local-a', toEntityId: 'public-a' },
      { fromEntityId: 'local-b', toEntityId: 'public-b' },
    ]);
    expect(submissionRow(db, first.id).status).toBe('approved');
    expect(submissionRow(db, second.id).status).toBe('approved');
    expect(db.prepare('SELECT * FROM player_library_conflict_resolution_audit').all()).toHaveLength(1);
  });

  test('leaves an unselected conflicting group pending without creating its redirect', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [entity('public-a', ['Alpha']), entity('public-b', ['Beta'])]);
    const selected = insertPending(db, [entity('local-a', ['Alpha'], ['game-a'])]);
    const unselected = insertPending(db, [entity('local-b', ['Beta'], ['game-b'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.suggestedTargetEntityId === 'public-a')!;

    const result = resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ]);

    expect(result).toMatchObject({ resolvedGroupCount: 1, associatedEntityCount: 1, pendingSubmissionCount: 1 });
    expect(submissionRow(db, selected.id).status).toBe('approved');
    expect(submissionRow(db, unselected.id)).toMatchObject({ status: 'pending', reviewed_at: null, review_reason: null });
    expect(listPlayerLibrary(db).entityRedirects).toEqual([
      { fromEntityId: 'local-a', toEntityId: 'public-a' },
    ]);
    expect(listPlayerLibrary(db).entities.find(item => item.entityId === 'public-b'))
      .toEqual(entity('public-b', ['Beta']));
  });

  test('rejects a stale pending payload token without any resolver mutation', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const input = insertPending(db, [entity('local-a', ['Alpha'])]);
    const state = readAdminConflictResolutionState(db);
    db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE id=?')
      .run(JSON.stringify({ entities: [entity('local-a', ['Changed'])] }), input.id);
    const before = mutationSnapshot(db);

    expectLibraryAdminError(() => resolveAdminConflictGroups(db, state.revision, 200, [
      { token: state.groups[0].token, targetEntityId: 'public-a' },
    ]), 409, 'stale_conflict_group');

    expect(mutationSnapshot(db)).toEqual(before);
  });

  test('rejects invalid decisions and targets without mutation', () => {
    const cases: Array<{
      name: string;
      expectedCode: string;
      makeDecisions: (groups: ReturnType<typeof readAdminConflictResolutionState>['groups']) => ConflictResolutionDecision[];
    }> = [
      { name: 'empty batch', expectedCode: 'invalid_request', makeDecisions: () => [] },
      {
        name: 'duplicate token',
        expectedCode: 'invalid_request',
        makeDecisions: groups => {
          const group = groups.find(item => item.kind === 'unique_name_target')!;
          const decision = { token: group.token, targetEntityId: group.suggestedTargetEntityId! };
          return [decision, decision];
        },
      },
      {
        name: 'mismatched unique target',
        expectedCode: 'invalid_conflict_target',
        makeDecisions: groups => [{
          token: groups.find(item => item.kind === 'unique_name_target')!.token,
          targetEntityId: 'public-outside',
        }],
      },
      {
        name: 'public merge target outside component',
        expectedCode: 'invalid_conflict_target',
        makeDecisions: groups => [{
          token: groups.find(item => item.kind === 'public_merge')!.token,
          targetEntityId: 'public-outside',
        }],
      },
    ];

    for (const scenario of cases) {
      const db = open();
      mutatePublicLibrary(db, 0, 100, 'import', [
        entity('public-unique', ['Unique']),
        entity('public-left', ['Left']),
        entity('public-right', ['Right']),
        entity('public-outside', ['Outside']),
      ]);
      insertPending(db, [entity('local-unique', ['Unique'])]);
      insertPending(db, [entity('public-left', ['Left', 'Right'])]);
      const state = readAdminConflictResolutionState(db);
      const before = mutationSnapshot(db);

      expectLibraryAdminError(
        () => resolveAdminConflictGroups(db, state.revision, 200, scenario.makeDecisions(state.groups)),
        400,
        scenario.expectedCode,
      );
      expect(mutationSnapshot(db), scenario.name).toEqual(before);
    }
  });

  test('never executes an ambiguous group and requires the exact public revision', () => {
    const ambiguousDb = open();
    insertPending(ambiguousDb, [entity('local-a', ['Same'])]);
    insertPending(ambiguousDb, [entity('local-b', ['Same'])]);
    const ambiguousState = readAdminConflictResolutionState(ambiguousDb);
    const ambiguousBefore = mutationSnapshot(ambiguousDb);
    expect(ambiguousState.groups[0].kind).toBe('ambiguous');

    expectLibraryAdminError(() => resolveAdminConflictGroups(ambiguousDb, ambiguousState.revision, 200, [{
      token: ambiguousState.groups[0].token,
      targetEntityId: 'local-a',
    }]), 400, 'invalid_conflict_target');
    expect(mutationSnapshot(ambiguousDb)).toEqual(ambiguousBefore);

    const staleRevisionDb = open();
    mutatePublicLibrary(staleRevisionDb, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    insertPending(staleRevisionDb, [entity('local-a', ['Alpha'])]);
    const staleState = readAdminConflictResolutionState(staleRevisionDb);
    const staleBefore = mutationSnapshot(staleRevisionDb);
    expectLibraryAdminError(() => resolveAdminConflictGroups(staleRevisionDb, staleState.revision - 1, 200, [{
      token: staleState.groups[0].token,
      targetEntityId: 'public-a',
    }]), 409, 'stale_revision');
    expect(mutationSnapshot(staleRevisionDb)).toEqual(staleBefore);
  });

  test('retargets and flattens redirects that point at a retired public source', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'import', [
      entity('public-a', ['Alpha'], ['game-a']),
      entity('public-b', ['Beta'], ['game-b']),
    ]);
    db.prepare("INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES('older-a','public-a')").run();
    insertPending(db, [entity('public-a', ['Alpha', 'Beta'], ['game-new'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'public_merge')!;

    resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-b' },
    ]);

    expect(listPlayerLibrary(db).entityRedirects).toEqual([
      { fromEntityId: 'older-a', toEntityId: 'public-b' },
      { fromEntityId: 'public-a', toEntityId: 'public-b' },
    ]);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('returns a structured size error and rolls back when a selected group exceeds the redirect limit', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const insert = db.prepare('INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES(?,?)');
    db.transaction(() => {
      for (let index = 0; index < MAX_ENTITY_REDIRECTS; index++) {
        insert.run(`retired-${index}`, 'public-a');
      }
    })();
    insertPending(db, [entity('local-a', ['Alpha'], ['new-game'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;
    const before = mutationSnapshot(db);

    expectLibraryAdminError(() => resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ]), 413, 'library_too_large');

    expect(mutationSnapshot(db)).toEqual(before);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('rolls back library, redirects, revision, originals and submissions when audit insertion aborts', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'], ['game-old'])]);
    const input = insertPending(db, [
      entity('local-a', ['Alpha', 'Accepted Alias'], ['game-new']),
      entity('clean', ['Clean'], ['later-game']),
    ], true);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;
    const before = mutationSnapshot(db);
    const identifierRows = db.prepare('SELECT * FROM player_identifiers ORDER BY identifier_id').all();
    db.exec(`CREATE TRIGGER fail_conflict_audit BEFORE INSERT ON player_library_conflict_resolution_audit
      BEGIN SELECT RAISE(ABORT,'fixture audit failure'); END`);

    expect(() => resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ])).toThrow('fixture audit failure');

    expect(mutationSnapshot(db)).toEqual(before);
    expect(db.prepare('SELECT * FROM player_identifiers ORDER BY identifier_id').all()).toEqual(identifierRows);
    expect(submissionRow(db, input.id).payload_json).toBe(input.payload);
    expect(db.pragma('foreign_key_check')).toEqual([]);
  });

  test('does not expose a clean automatic-only identity match as a conflict-resolution group', () => {
    const db = open();
    const games = ['g1', 'g2', 'g3', 'g4', 'g5'];
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'], games)]);
    insertPending(db, [entity('local-a', ['Other'], games)]);

    expect(readAdminConflictResolutionState(db).groups).toEqual([]);
  });

  test('expands an automatic peer into the authoritative projected name conflict and reuses supplied state', () => {
    const db = open();
    const games = ['g1', 'g2', 'g3', 'g4', 'g5'];
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const first = insertPending(db, [
      entity('local-name', ['Alpha'], games),
      entity('local-game', ['Other'], games),
    ]);
    db.prepare(`INSERT INTO player_library_submissions(device_id,payload_json,status,created_at)
      VALUES('invalid','{"entities":[]}','pending',100)`).run();

    const fresh = readAdminConflictResolutionState(db);
    expect(fresh.submissions.map(item => item.id)).toEqual([first.id]);
    expect(fresh.groups).toHaveLength(1);
    expect(fresh.groups[0]).toMatchObject({
      kind: 'unique_name_target',
      publicEntityIds: ['public-a'],
      suggestedTargetEntityId: 'public-a',
      sources: [{ entityId: 'local-game' }, { entityId: 'local-name' }],
    });

    const current = listPlayerLibrary(db);
    const alreadyLoaded = listAdminSubmissions(db, current.entities);
    const later = insertPending(db, [entity('local-later', ['Alpha'])]);
    const shared = buildAdminConflictResolutionState(db, current, alreadyLoaded);
    expect(shared.submissions.some(item => item.id === later.id)).toBe(false);
    expect(shared.groups.flatMap(item => item.sources).some(source => source.submissionId === later.id)).toBe(false);
    expect(readAdminConflictResolutionState(db).submissions.some(item => item.id === later.id)).toBe(true);
  });

  test('accepts duplicate source IDs across submissions without losing either payload union', () => {
    const db = open();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'], ['game-public'])]);
    const first = insertPending(db, [entity('same-local', ['Alpha', 'Alias One'], ['game-one'])]);
    const second = insertPending(db, [entity('same-local', ['Alpha', 'Alias Two'], ['game-two'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;

    const result = resolveAdminConflictGroups(db, state.revision, 200, [
      { token: group.token, targetEntityId: 'public-a' },
    ]);

    expect(result.associatedEntityCount).toBe(2);
    expect(listPlayerLibrary(db).entities[0].names).toEqual(expect.arrayContaining(['Alpha', 'Alias One', 'Alias Two']));
    expect(listPlayerLibrary(db).entities[0].gameIds).toEqual(expect.arrayContaining(['game-public', 'game-one', 'game-two']));
    expect(listPlayerLibrary(db).entityRedirects).toEqual([
      { fromEntityId: 'same-local', toEntityId: 'public-a' },
    ]);
    expect(submissionRow(db, first.id).status).toBe('approved');
    expect(submissionRow(db, second.id).status).toBe('approved');
  });
});

describe('guarded batch library conflict resolution API', () => {
  test('state exposes every server-computed group and exact kind stats regardless of filters', async () => {
    const { db, getState } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'import', [
      entity('public-unique', ['Unique']),
      entity('public-left', ['Left']),
      entity('public-right', ['Right']),
    ]);
    insertPending(db, [entity('local-unique', ['Unique'])]);
    insertPending(db, [entity('public-left', ['Left', 'Right'])]);
    insertPending(db, [entity('ambiguous-a', ['Ambiguous'])]);
    insertPending(db, [entity('ambiguous-b', ['Ambiguous'])]);
    const expected = readAdminConflictResolutionState(db);

    const unfiltered = (await getState().expect(200)).body;
    expect(unfiltered.conflictResolutionGroups).toEqual(expected.groups);
    expect(unfiltered.conflictResolutionGroups.map((group: { kind: string }) => group.kind).sort()).toEqual([
      'ambiguous',
      'public_merge',
      'unique_name_target',
    ]);
    expect(unfiltered.conflictResolutionGroups.every((group: { token: string }) => /^[a-f0-9]{64}$/.test(group.token))).toBe(true);
    expect(unfiltered.conflictResolutionStats).toEqual({ uniqueNameTargets: 1, publicMerges: 1, ambiguous: 1 });

    const filtered = (await getState('?q=missing&pendingQ=missing&filter=clean').expect(200)).body;
    expect(filtered.entities).toEqual([]);
    expect(filtered.submissions).toEqual([]);
    expect(filtered.conflictResolutionGroups).toEqual(unfiltered.conflictResolutionGroups);
    expect(filtered.conflictResolutionStats).toEqual(unfiltered.conflictResolutionStats);
    expect(filtered.stats).toEqual(unfiltered.stats);
  });

  test('resolves a selected token through HTTP and returns publication counts', async () => {
    const { db, postResolve } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Shared', 'Public Alias'], ['game-public'])]);
    const first = insertPending(db, [entity('local-a', ['Shared', 'Alias A'], ['game-a'])], true);
    const second = insertPending(db, [entity('local-b', ['Shared', 'Alias B'], ['game-b'])]);
    const state = readAdminConflictResolutionState(db);
    const group = state.groups.find(item => item.kind === 'unique_name_target')!;

    const response = await postResolve({
      revision: state.revision,
      confirm: true,
      groups: [{ token: group.token, targetEntityId: 'public-a' }],
    }).expect(200);

    expect(response.body).toEqual({
      ok: true,
      revision: 2,
      resolvedGroupCount: 1,
      associatedEntityCount: 2,
      mergedPublicEntityCount: 0,
      redirectCount: 2,
      pendingSubmissionCount: 0,
    });
    const library = listPlayerLibrary(db);
    expect(library).toMatchObject({
      revision: 2,
      entityRedirects: [
        { fromEntityId: 'local-a', toEntityId: 'public-a' },
        { fromEntityId: 'local-b', toEntityId: 'public-a' },
      ],
    });
    expect(library.entities).toEqual([expect.objectContaining({
      entityId: 'public-a',
      names: expect.arrayContaining(['Shared', 'Public Alias', 'Alias A', 'Alias B']),
      gameIds: expect.arrayContaining(['game-public', 'game-a', 'game-b']),
    })]);
    expect(submissionRow(db, first.id)).toMatchObject({ status: 'approved', reviewed_at: 200, review_reason: 'admin_conflicts_resolved' });
    expect(submissionRow(db, second.id)).toMatchObject({ status: 'approved', reviewed_at: 200, review_reason: 'admin_conflicts_resolved' });
    const audit = db.prepare('SELECT revision,created_at,groups_json FROM player_library_conflict_resolution_audit').get() as {
      revision: number;
      created_at: number;
      groups_json: string;
    };
    expect(audit).toMatchObject({ revision: 2, created_at: 200 });
    expect(JSON.parse(audit.groups_json)).toMatchObject([{
      targetEntityId: 'public-a',
      group: { token: group.token, kind: 'unique_name_target' },
    }]);
  });

  test('strict request validation rejects malformed batches without mutation', async () => {
    const { db, postResolve } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    insertPending(db, [entity('local-a', ['Alpha'])]);
    const state = readAdminConflictResolutionState(db);
    const decision = { token: state.groups[0].token, targetEntityId: 'public-a' };
    const tooMany = Array.from({ length: 101 }, (_, index) => ({
      token: index.toString(16).padStart(64, '0'),
      targetEntityId: 'public-a',
    }));
    const cases: Array<{ name: string; body: object }> = [
      { name: 'missing confirmation', body: { revision: state.revision, groups: [decision] } },
      { name: 'duplicate token', body: { revision: state.revision, confirm: true, groups: [decision, decision] } },
      { name: 'malformed token', body: { revision: state.revision, confirm: true,
        groups: [{ token: 'A'.repeat(64), targetEntityId: 'public-a' }] } },
      { name: 'invalid entity ID', body: { revision: state.revision, confirm: true,
        groups: [{ token: decision.token, targetEntityId: 'invalid entity' }] } },
      { name: 'empty list', body: { revision: state.revision, confirm: true, groups: [] } },
      { name: 'more than 100 decisions', body: { revision: state.revision, confirm: true, groups: tooMany } },
      { name: 'invalid request shape', body: { revision: state.revision, confirm: true, groups: 'invalid' } },
      { name: 'extra top-level browser data', body: { revision: state.revision, confirm: true, groups: [decision], targetName: 'Alpha' } },
      { name: 'extra decision browser data', body: { revision: state.revision, confirm: true,
        groups: [{ ...decision, names: ['Alpha'], sourceEntities: [] }] } },
    ];
    const before = mutationSnapshot(db);

    for (const scenario of cases) {
      const response = await postResolve(scenario.body).expect(400);
      expect(response.body, scenario.name).toMatchObject({ ok: false, code: 'invalid_request' });
      expect(mutationSnapshot(db), scenario.name).toEqual(before);
    }
  });

  test('requires Basic authentication and CSRF before resolving', async () => {
    const { app, db } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    insertPending(db, [entity('local-a', ['Alpha'])]);
    const state = readAdminConflictResolutionState(db);
    const body = { revision: state.revision, confirm: true,
      groups: [{ token: state.groups[0].token, targetEntityId: 'public-a' }] };
    const url = '/admin/api/library/conflicts/resolve';
    const before = mutationSnapshot(db);

    await request(app).post(url).set('x-dnf-admin-csrf', 'csrf').send(body).expect(401);
    await request(app).post(url).auth('admin', 'wrong').set('x-dnf-admin-csrf', 'csrf').send(body).expect(401);
    await request(app).post(url).auth('admin', 'password').send(body).expect(403, { ok: false, code: 'invalid_csrf' });
    expect(mutationSnapshot(db)).toEqual(before);
  });

  test('returns stale_conflict_group when a pending payload changes after state was read', async () => {
    const { db, getState, postResolve } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    const input = insertPending(db, [entity('local-a', ['Alpha'])]);
    const expected = readAdminConflictResolutionState(db);
    const state = (await getState().expect(200)).body;
    expect(state.conflictResolutionGroups).toEqual(expected.groups);
    db.prepare('UPDATE player_library_submissions SET payload_json=? WHERE id=?')
      .run(JSON.stringify({ entities: [entity('local-a', ['Changed'])] }), input.id);
    const before = mutationSnapshot(db);

    const response = await postResolve({
      revision: state.revision,
      confirm: true,
      groups: [{ token: state.conflictResolutionGroups[0].token, targetEntityId: 'public-a' }],
    }).expect(409);

    expect(response.body).toEqual({ ok: false, code: 'stale_conflict_group', conflicts: [] });
    expect(mutationSnapshot(db)).toEqual(before);
    expect(listPlayerLibrary(db).revision).toBe(1);
  });

  test('preserves the stale revision conflict contract without mutation', async () => {
    const { db, postResolve } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'create', [entity('public-a', ['Alpha'])]);
    insertPending(db, [entity('local-a', ['Alpha'])]);
    const state = readAdminConflictResolutionState(db);
    const before = mutationSnapshot(db);

    const response = await postResolve({
      revision: state.revision - 1,
      confirm: true,
      groups: [{ token: state.groups[0].token, targetEntityId: 'public-a' }],
    }).expect(409);

    expect(response.body).toEqual({ ok: false, code: 'stale_revision', conflicts: [] });
    expect(mutationSnapshot(db)).toEqual(before);
  });

  test('rejects ambiguous groups and targets outside a public-merge component through HTTP', async () => {
    const { db, postResolve } = httpFixture();
    mutatePublicLibrary(db, 0, 100, 'import', [
      entity('public-left', ['Left']),
      entity('public-right', ['Right']),
      entity('public-outside', ['Outside']),
    ]);
    insertPending(db, [entity('public-left', ['Left', 'Right'])]);
    insertPending(db, [entity('ambiguous-a', ['Ambiguous'])]);
    insertPending(db, [entity('ambiguous-b', ['Ambiguous'])]);
    const state = readAdminConflictResolutionState(db);
    const ambiguous = state.groups.find(group => group.kind === 'ambiguous')!;
    const publicMerge = state.groups.find(group => group.kind === 'public_merge')!;
    const before = mutationSnapshot(db);

    for (const decision of [
      { token: ambiguous.token, targetEntityId: 'ambiguous-a' },
      { token: publicMerge.token, targetEntityId: 'public-outside' },
    ]) {
      const response = await postResolve({ revision: state.revision, confirm: true, groups: [decision] }).expect(400);
      expect(response.body).toEqual({ ok: false, code: 'invalid_conflict_target', conflicts: [] });
      expect(mutationSnapshot(db)).toEqual(before);
    }
  });
});
