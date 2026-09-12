import { createHash, randomUUID } from 'node:crypto';
import type Database from 'better-sqlite3';
import { z } from 'zod';
import { canonicalizeIdentifiers, MAX_PLAYER_ENTITY_VALUES, type PlayerEntity } from './player-library.js';
import { playerNameSchema } from './schemas.js';
import { canonicalEntityRedirects, listPlayerLibrary, MAX_AUTOMATIC_EVIDENCE_BYTES, MAX_ENTITY_REDIRECTS, readAutomaticIdentityEvidence, readEntityRedirects, type EntityRedirect } from './library-store.js';
import { createSubmissionReconciler } from './library-submission-reconcile.js';
import { automaticIdentityGroups, type AutomaticIdentityEvidence } from './library-identity-policy.js';
import { buildConflictResolutionGroups } from './library-conflict-resolution.js';

export const LIBRARY_ADMIN_MAX_BYTES = 256 * 1024;
export const revisionSchema = z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER);
export const libraryReviewGuardSchema = z.object({ revision: revisionSchema, submissionRevision: z.string().regex(/^[a-f0-9]{64}$/) }).strict();
export const entityIdSchema = z.string().min(1).max(128).regex(/^[A-Za-z0-9_-]+$/);
const valueSchema = z.string().max(128).refine(value => value.trim().length > 0 && !/[\u0000-\u001f]/u.test(value));
export const adminEntitySchema = z.object({
  entityId: entityIdSchema.optional(),
  names: z.array(playerNameSchema.pipe(valueSchema)).min(1).max(MAX_PLAYER_ENTITY_VALUES),
  gameIds: z.array(valueSchema).max(MAX_PLAYER_ENTITY_VALUES),
}).strip();
const payloadSchema = z.object({ entities: z.array(adminEntitySchema).min(1).max(10_000) }).strict();
const fields = ['names', 'gameIds'] as const;
export interface OwnershipConflict { kind: typeof fields[number]; value: string; entityIds: string[] }
export class LibraryAdminError extends Error {
  constructor(public status: number, public code: string, public conflicts: OwnershipConflict[] = []) { super(code); }
}
const key = (value: string) => value.normalize('NFC').trim().toLocaleLowerCase();
function boundedJson(text: string): unknown {
  if (Buffer.byteLength(text, 'utf8') > LIBRARY_ADMIN_MAX_BYTES) throw new LibraryAdminError(413, 'library_too_large');
  // Bound nesting before JSON.parse, including adversarial arrays unrelated to the expected schema.
  let depth = 0; let quoted = false; let escaped = false;
  for (const character of text) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === '"') quoted = false;
    } else if (character === '"') quoted = true;
    else if (character === '{' || character === '[') {
      if (++depth > 16) throw new LibraryAdminError(400, 'invalid_library');
    } else if (character === '}' || character === ']') depth--;
  }
  try { return JSON.parse(text); } catch { throw new LibraryAdminError(400, 'invalid_library'); }
}
export function parseAdminEntities(payload: unknown, fallbackId?: (index: number) => string): PlayerEntity[] {
  const parsed = payloadSchema.safeParse(payload);
  if (!parsed.success) throw new LibraryAdminError(400, 'invalid_library');
  if (Buffer.byteLength(JSON.stringify(payload), 'utf8') > LIBRARY_ADMIN_MAX_BYTES) throw new LibraryAdminError(413, 'library_too_large');
  const entities = parsed.data.entities.map((entity, index) => ({
    entityId: entity.entityId ?? fallbackId?.(index) ?? `player-${randomUUID()}`,
    names: canonicalizeIdentifiers(entity.names), gameIds: canonicalizeIdentifiers(entity.gameIds),
  }));
  if (new Set(entities.map(entity => entity.entityId)).size !== entities.length) throw new LibraryAdminError(400, 'duplicate_entity_id');
  return entities;
}
export function parseLibraryImport(text: string): PlayerEntity[] {
  if (Buffer.byteLength(text, 'utf8') > LIBRARY_ADMIN_MAX_BYTES) throw new LibraryAdminError(413, 'library_too_large');
  text = text.replace(/^\uFEFF/u, '').trim();
  if (text.startsWith('{') || text.startsWith('[')) {
    const envelope = z.object({ entities: z.unknown(), revision: revisionSchema.optional(), schemaVersion: z.literal(2).optional() }).strict().safeParse(boundedJson(text));
    if (!envelope.success) throw new LibraryAdminError(400, 'invalid_library');
    return parseAdminEntities({ entities: envelope.data.entities });
  }
  const rows = new Map<string, PlayerEntity>();
  for (const [index, raw] of text.split(/\r?\n/u).entries()) {
    const line = raw.trim();
    if (!line || line.startsWith(';') || line.startsWith('//')) continue;
    const separator = line.search(/[=＝]/u);
    const name = line.slice(0, separator).trim();
    const ids = line.slice(separator + 1).trim();
    if (separator < 1 || !ids) throw new LibraryAdminError(400, `invalid_import_line_${index + 1}`);
    let values: string[];
    if (/[()（）]/u.test(ids)) {
      values = [];
      let cursor = 0;
      while (cursor < ids.length) {
        if (/\s/u.test(ids[cursor])) { cursor++; continue; }
        const close = ids[cursor] === '(' ? ')' : ids[cursor] === '（' ? '）' : '';
        const end = close ? ids.indexOf(close, cursor + 1) : -1;
        const value = ids.slice(cursor + 1, end);
        if (end < 0 || !value.trim() || /[()（）]/u.test(value)) throw new LibraryAdminError(400, `invalid_import_line_${index + 1}`);
        values.push(value); cursor = end + 1;
      }
    } else values = ids.split(/[,，;；]/u);
    const row = rows.get(key(name)) ?? { entityId: `legacy-${createHash('sha256').update(key(name)).digest('hex').slice(0, 24)}`, names: [name], gameIds: [] };
    row.gameIds.push(...values);
    rows.set(key(name), row);
  }
  return parseAdminEntities({ entities: [...rows.values()] });
}
export function ownershipConflicts(entities: readonly PlayerEntity[]): OwnershipConflict[] {
  const result: OwnershipConflict[] = [];
  for (const field of ['names'] as const) {
    const owners = new Map<string, { value: string; ids: Set<string> }>();
    for (const entity of entities) for (const value of entity[field]) {
      const entry = owners.get(key(value)) ?? { value, ids: new Set<string>() };
      entry.ids.add(entity.entityId); owners.set(key(value), entry);
    }
    for (const entry of owners.values()) if (entry.ids.size > 1) result.push({ kind: field, value: entry.value, entityIds: [...entry.ids] });
  }
  return result;
}
export function requireLibraryRevision(db: Database.Database, revision: number) {
  const library = listPlayerLibrary(db);
  if (library.revision !== revision) throw new LibraryAdminError(409, 'stale_revision');
  return library;
}
function additiveEntities(existing: PlayerEntity[], incoming: PlayerEntity[]): PlayerEntity[] {
  const byId = new Map(existing.map(entity => [entity.entityId, structuredClone(entity)]));
  for (const entity of incoming) {
    const current = byId.get(entity.entityId);
    if (!current) byId.set(entity.entityId, structuredClone(entity));
    else for (const field of fields) current[field] = canonicalizeIdentifiers([...current[field], ...entity[field]]);
  }
  return [...byId.values()];
}
function normalizePublicEntities(previous: PlayerEntity[], incoming: PlayerEntity[], evidence: AutomaticIdentityEvidence = new Map(), automatic = true) {
  const existing = new Set(previous.map(entity => entity.entityId));
  const byId = new Map(incoming.map(entity => [entity.entityId, entity]));
  const targets = new Map<string, string>();
  const groups = (automatic ? automaticIdentityGroups(incoming, evidence) : []).map(ids => ids.sort((a, b) =>
    Number(existing.has(b)) - Number(existing.has(a)) || (a < b ? -1 : a > b ? 1 : 0)));
  const nextEvidence = new Map([...evidence].filter(([id]) => byId.has(id)));
  const members = new Map<string, string[]>();
  for (const ids of groups) {
    const target = ids[0];
    // Capture each constituent before replacing the target with its public union.
    const originals = ids.flatMap(id => evidence.get(id) ?? [byId.get(id)!]);
    for (const id of ids) nextEvidence.delete(id);
    nextEvidence.set(target, originals);
    members.set(target, ids);
    for (const id of ids) targets.set(id, target);
    byId.set(target, additiveEntities([byId.get(target)!], ids.slice(1).map(id => ({ ...byId.get(id)!, entityId: target })))[0]);
  }
  const entities = incoming.filter(entity => !targets.has(entity.entityId) || targets.get(entity.entityId) === entity.entityId)
    .map(entity => byId.get(entity.entityId)!);
  // Report original members so partial review blocks every source represented by a conflicting union.
  const conflicts = ownershipConflicts(entities).map(conflict => ({ ...conflict,
    entityIds: conflict.entityIds.flatMap(id => members.get(id) ?? [id]) }));
  return { entities, conflicts, groups, targets, evidence: nextEvidence };
}
export function projectedLibrary(existing: PlayerEntity[], incoming: PlayerEntity[], evidence: AutomaticIdentityEvidence = new Map()) {
  const { entities, conflicts, groups } = normalizePublicEntities(existing, additiveEntities(existing, incoming), evidence);
  return { entities, conflicts, automaticGroups: groups };
}
function publishEntities(db: Database.Database, entities: PlayerEntity[], previous: PlayerEntity[], now: number,
  entityRedirects = readEntityRedirects(db, previous), manualTargetIds?: ReadonlySet<string>): number {
  entities = entities.length ? parseAdminEntities({ entities }) : [];
  const beforeRedirects = readEntityRedirects(db, previous);
  const beforeEvidence = readAutomaticIdentityEvidence(db);
  const sourceEntities = new Map(entities.map(entity => [entity.entityId, entity]));
  const normalized = normalizePublicEntities(previous, entities, beforeEvidence, manualTargetIds === undefined);
  for (const targetEntityId of manualTargetIds ?? []) normalized.evidence.delete(targetEntityId);
  const { conflicts, groups, targets } = normalized;
  entities = normalized.entities;
  if (conflicts.length) throw new LibraryAdminError(409, 'ownership_conflict', conflicts);
  entityRedirects = [
    ...entityRedirects.map(item => ({ ...item, toEntityId: targets.get(item.toEntityId) ?? item.toEntityId })),
    ...groups.flatMap(ids => ids.slice(1).map(fromEntityId => ({ fromEntityId, toEntityId: ids[0] }))),
  ];
  if (entityRedirects.length > MAX_ENTITY_REDIRECTS) throw new LibraryAdminError(413, 'library_too_large');
  entityRedirects = canonicalEntityRedirects(entities, entityRedirects);
  if (Buffer.byteLength(JSON.stringify({ revision: Number.MAX_SAFE_INTEGER, entities, entityRedirects }), 'utf8') > LIBRARY_ADMIN_MAX_BYTES) throw new LibraryAdminError(413, 'library_too_large');
  // Validate the resulting additive union as well as each incoming entity.
  if (entities.length) parseAdminEntities({ entities });
  const evidenceRows = [...normalized.evidence].map(([id, originals]) => {
    if (originals.length > MAX_PLAYER_ENTITY_VALUES) throw new LibraryAdminError(413, 'library_too_large');
    return { id, json: JSON.stringify(originals) };
  });
  if (evidenceRows.reduce((bytes, row) => bytes + Buffer.byteLength(row.json, 'utf8'), 0) > MAX_AUTOMATIC_EVIDENCE_BYTES) {
    throw new LibraryAdminError(413, 'library_too_large');
  }
  const next = new Map(entities.map(entity => [entity.entityId, entity]));
  const old = new Map(previous.map(entity => [entity.entityId, entity]));
  let changed = JSON.stringify(beforeRedirects) !== JSON.stringify(entityRedirects);
  const same = (a: PlayerEntity, b: PlayerEntity) => fields.every(field =>
    JSON.stringify([...a[field]].sort()) === JSON.stringify([...b[field]].sort()));
  // Existing redirect targets must move before their old entity is cascade-deleted.
  for (const redirect of entityRedirects) {
    db.prepare('UPDATE player_entity_redirects SET to_entity_id=? WHERE from_entity_id=? AND to_entity_id<>?')
      .run(redirect.toEntityId, redirect.fromEntityId, redirect.toEntityId);
  }
  for (const entity of previous) if (!next.has(entity.entityId)) {
    db.prepare('DELETE FROM player_entities WHERE entity_id = ?').run(entity.entityId); changed = true;
  }
  // Remove changed ownership rows together before inserting the validated final projection.
  const updates = entities.filter(entity => !old.has(entity.entityId) || !same(entity, old.get(entity.entityId)!));
  for (const entity of updates) {
    db.prepare('DELETE FROM player_entity_names WHERE entity_id = ?').run(entity.entityId);
    db.prepare('DELETE FROM player_entity_identifiers WHERE entity_id = ?').run(entity.entityId);
  }
  for (const entity of updates) {
    db.prepare(`INSERT INTO player_entities (entity_id,created_at,updated_at) VALUES (?,?,?)
      ON CONFLICT(entity_id) DO UPDATE SET updated_at=excluded.updated_at`).run(entity.entityId, now, now);
    for (const name of entity.names) db.prepare('INSERT INTO player_entity_names (entity_id,name_norm,display_name) VALUES (?,?,?)').run(entity.entityId, key(name), name);
    for (const value of entity.gameIds) {
        db.prepare("INSERT INTO player_identifiers(kind,identifier_norm) VALUES('game',?) ON CONFLICT(kind,identifier_norm) DO NOTHING").run(key(value));
        db.prepare(`INSERT INTO player_identifier_spellings(identifier_id,display_value)
          SELECT identifier_id,? FROM player_identifiers WHERE kind='game' AND identifier_norm=?
          ON CONFLICT(identifier_id,display_value) DO NOTHING`).run(value, key(value));
        db.prepare(`INSERT INTO player_entity_identifiers(entity_id,identifier_id,spelling_id)
          SELECT ?,identifier_id,spelling_id FROM player_identifier_spellings JOIN player_identifiers USING(identifier_id)
          WHERE kind='game' AND identifier_norm=? AND display_value=?`).run(entity.entityId, key(value), value);
    }
    changed = true;
  }
  for (const redirect of entityRedirects) {
    db.prepare('INSERT INTO player_entity_redirects(from_entity_id,to_entity_id) VALUES(?,?) ON CONFLICT(from_entity_id) DO NOTHING')
      .run(redirect.fromEntityId, redirect.toEntityId);
  }
  for (const id of beforeEvidence.keys()) if (!normalized.evidence.has(id)) {
    db.prepare('DELETE FROM player_entity_auto_evidence WHERE entity_id=?').run(id);
    changed = true;
  }
  for (const row of evidenceRows) if (row.json !== JSON.stringify(beforeEvidence.get(row.id))) {
    db.prepare(`INSERT INTO player_entity_auto_evidence(entity_id,evidence_json) VALUES(?,?)
      ON CONFLICT(entity_id) DO UPDATE SET evidence_json=excluded.evidence_json`).run(row.id, row.json);
    changed = true;
  }
  if (changed) db.prepare('UPDATE player_library_meta SET revision=revision+1 WHERE id=1').run();
  const revision = (db.prepare('SELECT revision FROM player_library_meta WHERE id=1').get() as { revision: number }).revision;
  for (const ids of groups) {
    db.prepare(`INSERT INTO player_library_merge_audit(target_entity_id,revision,created_at,before_json,before_redirects_json,after_json,redirects_json,merge_kind,before_evidence_json,after_evidence_json)
      VALUES(?,?,?,?,?,?,?,'automatic',?,?)`).run(ids[0], revision, now,
      JSON.stringify(ids.map(id => old.get(id) ?? sourceEntities.get(id)!)), JSON.stringify(beforeRedirects),
      JSON.stringify(next.get(ids[0])), JSON.stringify(entityRedirects),
      JSON.stringify(Object.fromEntries(ids.map(id => [id, beforeEvidence.get(id) ?? [sourceEntities.get(id)!]]))),
      JSON.stringify({ [ids[0]]: normalized.evidence.get(ids[0]) }));
  }
  return revision;
}
export function mutatePublicLibrary(db: Database.Database, revision: number, now: number,
  mode: 'create' | 'replace' | 'delete' | 'import', incoming: PlayerEntity[], entityId?: string) {
  return db.transaction(() => {
    const current = requireLibraryRevision(db, revision);
    if (incoming.some(entity => current.entityRedirects.some(item => item.fromEntityId === entity.entityId))) {
      throw new LibraryAdminError(409, 'retired_entity_id');
    }
    const id = entityId ?? incoming[0]?.entityId;
    const found = current.entities.some(entity => entity.entityId === id);
    if (mode === 'create' && found) throw new LibraryAdminError(409, 'entity_already_exists');
    if ((mode === 'replace' || mode === 'delete') && !found) throw new LibraryAdminError(404, 'entity_not_found');
    if (mode === 'delete' && current.entityRedirects.some(item => item.toEntityId === id)) {
      throw new LibraryAdminError(409, 'entity_has_redirects');
    }
    const projected = mode === 'import' ? additiveEntities(current.entities, incoming)
      : [...current.entities.filter(entity => mode === 'create' || entity.entityId !== id), ...incoming];
    const nextRevision = publishEntities(db, projected, current.entities, now);
    const published = listPlayerLibrary(db);
    const targetId = published.entityRedirects.find(item => item.fromEntityId === incoming[0]?.entityId)?.toEntityId ?? incoming[0]?.entityId;
    return { revision: nextRevision, entity: published.entities.find(entity => entity.entityId === targetId) };
  }).immediate();
}

export function mergePublicLibrary(db: Database.Database, revision: number, now: number, targetEntityId: string, entityIds: string[]) {
  return db.transaction(() => {
    const current = requireLibraryRevision(db, revision);
    const selected = new Set(entityIds);
    if (selected.size < 2 || selected.size !== entityIds.length || !selected.has(targetEntityId)) throw new LibraryAdminError(400, 'invalid_request');
    const before = current.entities.filter(entity => selected.has(entity.entityId));
    if (before.length !== selected.size) throw new LibraryAdminError(404, 'entity_not_found');
    const target = before.find(entity => entity.entityId === targetEntityId)!;
    const entity = additiveEntities([target], before.filter(item => item !== target).map(item => ({ ...item, entityId: targetEntityId })))[0];
    const projected = [...current.entities.filter(item => !selected.has(item.entityId)), entity];
    const redirects: EntityRedirect[] = [
      ...current.entityRedirects.map(item => ({ ...item, toEntityId: selected.has(item.toEntityId) ? targetEntityId : item.toEntityId })),
      ...entityIds.filter(id => id !== targetEntityId).map(fromEntityId => ({ fromEntityId, toEntityId: targetEntityId })),
    ];
    if (redirects.length > MAX_ENTITY_REDIRECTS) throw new LibraryAdminError(413, 'library_too_large');
    const entityRedirects = canonicalEntityRedirects(projected, redirects);
    const beforeEvidence = readAutomaticIdentityEvidence(db);
    const nextRevision = publishEntities(db, projected, current.entities, now, entityRedirects, new Set([targetEntityId]));
    const published = listPlayerLibrary(db);
    const canonicalId = published.entityRedirects.find(item => item.fromEntityId === targetEntityId)?.toEntityId ?? targetEntityId;
    const publishedEntity = published.entities.find(item => item.entityId === canonicalId)!;
    db.prepare(`INSERT INTO player_library_merge_audit(target_entity_id,revision,created_at,before_json,before_redirects_json,after_json,redirects_json,merge_kind,before_evidence_json,after_evidence_json)
      VALUES(?,?,?,?,?,?,?,'manual',?,?)`).run(canonicalId, nextRevision, now, JSON.stringify(before), JSON.stringify(current.entityRedirects), JSON.stringify(publishedEntity), JSON.stringify(published.entityRedirects),
      JSON.stringify(Object.fromEntries(before.map(item => [item.entityId, beforeEvidence.get(item.entityId) ?? [item]]))),
      JSON.stringify({ [canonicalId]: [publishedEntity] }));
    return { revision: nextRevision, entity: publishedEntity, entityRedirects: published.entityRedirects };
  }).immediate();
}
export function readAdminSubmission(db: Database.Database, id: number, publicEntities = listPlayerLibrary(db).entities,
  reconcile = createSubmissionReconciler(publicEntities, readEntityRedirects(db, publicEntities)),
  evidence: AutomaticIdentityEvidence = readAutomaticIdentityEvidence(db)) {
  const row = db.prepare('SELECT * FROM player_library_submissions WHERE id = ?').get(id) as
    { id: number; device_id: string; payload_json: string; status: string; created_at: number; review_reason: string | null } | undefined;
  if (!row) throw new LibraryAdminError(404, 'submission_not_found');
  let entities: PlayerEntity[] = []; let valid = true;
  try { entities = parseAdminEntities(boundedJson(row.payload_json), index => `submission-${id}-${index}`); } catch { valid = false; }
  const analysis = reconcile(entities);
  const { conflicts, automaticGroups } = projectedLibrary(publicEntities, analysis.entities, evidence);
  return { id, deviceId: row.device_id, createdAt: row.created_at, status: row.status, valid, ...analysis, conflicts, automaticGroups,
    reviewReason: row.review_reason,
    submissionRevision: createHash('sha256').update(JSON.stringify([row.payload_json, row.status])).digest('hex') };
}
export function listAdminSubmissions(db: Database.Database, publicEntities: PlayerEntity[]) {
  const reconcile = createSubmissionReconciler(publicEntities, readEntityRedirects(db, publicEntities));
  const evidence = readAutomaticIdentityEvidence(db);
  return (db.prepare("SELECT id FROM player_library_submissions WHERE status='pending' ORDER BY created_at,id").all() as { id: number }[])
    .map(row => readAdminSubmission(db, row.id, publicEntities, reconcile, evidence));
}

export function buildAdminConflictResolutionState(db: Database.Database,
  current: ReturnType<typeof listPlayerLibrary>,
  allSubmissions = listAdminSubmissions(db, current.entities)) {
  const submissions = allSubmissions.filter(submission => submission.valid);
  const combined = additiveEntities(current.entities, submissions.flatMap(submission => submission.entities));
  const automaticGroups = automaticIdentityGroups(combined, readAutomaticIdentityEvidence(db));
  const conflicts = [
    ...ownershipConflicts(combined),
    ...automaticGroups.map(entityIds => ({ kind: 'gameIds' as const, value: 'automatic_identity', entityIds })),
  ];
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

export interface ConflictResolutionDecision { token: string; targetEntityId: string }

export function resolveAdminConflictGroups(db: Database.Database, revision: number, now: number,
  decisions: ConflictResolutionDecision[]) {
  return db.transaction(() => {
    const current = requireLibraryRevision(db, revision);
    const state = buildAdminConflictResolutionState(db, current);
    if (!decisions.length || new Set(decisions.map(decision => decision.token)).size !== decisions.length) {
      throw new LibraryAdminError(400, 'invalid_request');
    }

    const available = new Map(state.groups.map(group => [group.token, group]));
    const selected = decisions.map(decision => {
      const group = available.get(decision.token);
      if (!group) throw new LibraryAdminError(409, 'stale_conflict_group');
      const validTarget = group.kind === 'unique_name_target'
        ? decision.targetEntityId === group.suggestedTargetEntityId
        : group.kind === 'public_merge' && group.publicEntityIds.includes(decision.targetEntityId);
      if (!validTarget) throw new LibraryAdminError(400, 'invalid_conflict_target');
      return { group, targetEntityId: decision.targetEntityId };
    });

    const publicIds = new Set(current.entities.map(entity => entity.entityId));
    const targetById = new Map<string, string>();
    const selectedSourceKeys = new Set<string>();
    const manualTargets = new Set<string>();
    for (const { group, targetEntityId } of selected) {
      manualTargets.add(targetEntityId);
      for (const entityId of [...group.publicEntityIds, ...group.sources.map(source => source.entityId)]) {
        const previousTarget = targetById.get(entityId);
        if (previousTarget !== undefined && previousTarget !== targetEntityId) {
          throw new LibraryAdminError(409, 'conflicting_resolution');
        }
        targetById.set(entityId, targetEntityId);
      }
      for (const source of group.sources) {
        selectedSourceKeys.add(`${source.submissionId}\0${source.entityId}`);
      }
    }

    const next = new Map(current.entities.map(entity => [entity.entityId, structuredClone(entity)]));
    const append = (targetEntityId: string, source: PlayerEntity) => {
      const target = next.get(targetEntityId);
      if (!target) throw new LibraryAdminError(404, 'entity_not_found');
      next.set(targetEntityId, additiveEntities([target], [{ ...source, entityId: targetEntityId }])[0]);
    };
    for (const entity of current.entities) {
      const targetEntityId = targetById.get(entity.entityId);
      if (targetEntityId !== undefined && targetEntityId !== entity.entityId) {
        append(targetEntityId, entity);
        next.delete(entity.entityId);
      }
    }
    for (const { group, targetEntityId } of selected) {
      for (const source of group.sources) append(targetEntityId, source);
    }

    const projected = [...next.values()];
    const redirects = canonicalEntityRedirects(projected, [
      ...current.entityRedirects.map(redirect => ({
        ...redirect,
        toEntityId: targetById.get(redirect.toEntityId) ?? redirect.toEntityId,
      })),
      ...[...targetById]
        .filter(([fromEntityId, toEntityId]) => fromEntityId !== toEntityId)
        .map(([fromEntityId, toEntityId]) => ({ fromEntityId, toEntityId })),
    ]);

    const nextRevision = publishEntities(db, projected, current.entities, now, redirects, manualTargets);
    let associatedEntityCount = 0;
    const affectedSubmissions: { id: number; submissionRevision: string }[] = [];
    for (const submission of state.submissions) {
      const acceptedIndexes = submission.entityTargets
        .map((entityId, index) => selectedSourceKeys.has(`${submission.id}\0${entityId}`) ? index : -1)
        .filter(index => index >= 0);
      if (!acceptedIndexes.length) continue;

      const row = db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(submission.id) as
        { payload_json: string } | undefined;
      if (!row) throw new LibraryAdminError(409, 'stale_conflict_group');
      const raw = boundedJson(row.payload_json) as {
        entities: Array<Omit<PlayerEntity, 'entityId'> & { entityId?: string }>;
      };
      db.prepare(`INSERT OR IGNORE INTO player_library_submission_originals(submission_id,payload_json)
        VALUES(?,?)`).run(submission.id, row.payload_json);
      const accepted = new Set(acceptedIndexes);
      const remaining = raw.entities.flatMap((entity, index) => accepted.has(index) ? [] : [{
        ...entity,
        entityId: entity.entityId ?? `submission-${submission.id}-${index}`,
      }]);
      db.prepare(`UPDATE player_library_submissions
        SET payload_json=?,status=?,reviewed_at=?,review_reason=? WHERE id=?`).run(
        remaining.length ? JSON.stringify({ entities: remaining }) : row.payload_json,
        remaining.length ? 'pending' : 'approved',
        now,
        remaining.length ? 'admin_conflicts_partially_resolved' : 'admin_conflicts_resolved',
        submission.id,
      );
      associatedEntityCount += acceptedIndexes.length;
      affectedSubmissions.push({ id: submission.id, submissionRevision: submission.submissionRevision });
    }

    const published = listPlayerLibrary(db);
    db.prepare(`INSERT INTO player_library_conflict_resolution_audit
      (revision,created_at,groups_json,before_entities_json,after_entities_json,before_redirects_json,after_redirects_json,submissions_json)
      VALUES(?,?,?,?,?,?,?,?)`).run(
        nextRevision,
        now,
        JSON.stringify(selected),
        JSON.stringify(current.entities),
        JSON.stringify(published.entities),
        JSON.stringify(current.entityRedirects),
        JSON.stringify(published.entityRedirects),
        JSON.stringify(affectedSubmissions),
      );
    const pendingSubmissionCount = (db.prepare(`SELECT COUNT(*) AS count FROM player_library_submissions
      WHERE status='pending'`).get() as { count: number }).count;
    return {
      revision: nextRevision,
      resolvedGroupCount: selected.length,
      associatedEntityCount,
      mergedPublicEntityCount: [...targetById]
        .filter(([fromEntityId, toEntityId]) => fromEntityId !== toEntityId && publicIds.has(fromEntityId)).length,
      redirectCount: published.entityRedirects.length - current.entityRedirects.length,
      pendingSubmissionCount,
    };
  }).immediate();
}

function requireSubmission(db: Database.Database, id: number, token: string, publicEntities = listPlayerLibrary(db).entities,
  reconcile = createSubmissionReconciler(publicEntities, readEntityRedirects(db, publicEntities)),
  evidence: AutomaticIdentityEvidence = readAutomaticIdentityEvidence(db)) {
  const submission = readAdminSubmission(db, id, publicEntities, reconcile, evidence);
  if (submission.status !== 'pending') throw new LibraryAdminError(409, 'submission_not_pending');
  if (submission.submissionRevision !== token) throw new LibraryAdminError(409, 'stale_submission');
  return submission;
}
export function editAdminSubmission(db: Database.Database, revision: number, id: number, token: string, entities: PlayerEntity[],
  confirmReconciliations = false) {
  entities = parseAdminEntities({ entities });
  return db.transaction(() => {
    requireLibraryRevision(db, revision);
    const submission = requireSubmission(db, id, token);
    if (submission.reconciliations.length && !confirmReconciliations) {
      throw new LibraryAdminError(409, 'reconciliation_confirmation_required');
    }
    // Explicit draft edits may accept a derived public ID; retain the unedited source for audit.
    db.prepare(`INSERT OR IGNORE INTO player_library_submission_originals (submission_id,payload_json)
      SELECT id,payload_json FROM player_library_submissions WHERE id=?`).run(id);
    db.prepare("UPDATE player_library_submissions SET payload_json=?,review_reason='admin_edited' WHERE id=?").run(JSON.stringify({ entities }), id);
    return { revision, submission: readAdminSubmission(db, id) };
  }).immediate();
}
export function reviewAdminSubmissions(db: Database.Database, revision: number, now: number, action: 'approve' | 'reject',
  refs: { id: number; submissionRevision: string }[], skipConflicts = false) {
  return db.transaction(() => {
    const current = requireLibraryRevision(db, revision);
    const reconcile = createSubmissionReconciler(current.entities, current.entityRedirects);
    const evidence = readAutomaticIdentityEvidence(db);
    const submissions = refs.map(ref => requireSubmission(db, ref.id, ref.submissionRevision, current.entities, reconcile, evidence));
    let nextRevision = revision;
    let conflicts: OwnershipConflict[] = [];
    const blocked = new Set<string>();
    let acceptedEntityCount = 0;
    const skippedEntities: { submissionId: number; submittedEntityId: string; entityId: string; names: string[]; conflicts: OwnershipConflict[] }[] = [];
    if (action === 'approve') {
      if (submissions.some(submission => !submission.valid)) throw new LibraryAdminError(400, 'invalid_library');
      // Analyze the entire projection before choosing any winner, including additions to known owners.
      const incoming = submissions.flatMap(submission => submission.entities);
      conflicts = projectedLibrary(current.entities, incoming, evidence).conflicts;
      if (conflicts.length && !skipConflicts) throw new LibraryAdminError(409, 'ownership_conflict', conflicts);
      for (const conflict of conflicts) for (const id of conflict.entityIds) blocked.add(id);
      const accepted = incoming.filter(entity => !blocked.has(entity.entityId));
      if (accepted.length) nextRevision = publishEntities(db, additiveEntities(current.entities, accepted), current.entities, now);
    }
    const pendingIds: number[] = [];
    for (const submission of submissions) {
      const skipped = submission.entityTargets.map((id, index) => blocked.has(id) ? index : -1).filter(index => index >= 0);
      if (action === 'approve') acceptedEntityCount += submission.rawEntityCount - skipped.length;
      if (skipped.length) {
        const row = db.prepare('SELECT payload_json FROM player_library_submissions WHERE id=?').get(submission.id) as { payload_json: string };
        const raw = boundedJson(row.payload_json) as { entities: PlayerEntity[] };
        // Preserve the exact source before rewriting pending rows, including prior admin edits.
        db.prepare('INSERT OR IGNORE INTO player_library_submission_originals(submission_id,payload_json) VALUES(?,?)').run(submission.id, row.payload_json);
        const remaining = skipped.map(index => {
          const source = raw.entities[index];
          const submittedEntityId = source.entityId ?? `submission-${submission.id}-${index}`;
          const entityId = submission.entityTargets[index];
          skippedEntities.push({ submissionId: submission.id, submittedEntityId, entityId, names: source.names,
            conflicts: conflicts.filter(conflict => conflict.entityIds.includes(entityId)) });
          return { ...source, entityId: submittedEntityId };
        });
        const partial = skipped.length < submission.rawEntityCount;
        db.prepare('UPDATE player_library_submissions SET payload_json=?,reviewed_at=?,review_reason=? WHERE id=?')
          .run(partial ? JSON.stringify({ entities: remaining }) : row.payload_json, now,
            partial || submission.reviewReason === 'admin_partially_approved' ? 'admin_partially_approved' : 'admin_conflicts_pending', submission.id);
        pendingIds.push(submission.id);
      } else {
        db.prepare('UPDATE player_library_submissions SET status=?,reviewed_at=?,review_reason=? WHERE id=?')
          .run(action === 'approve' ? 'approved' : 'rejected', now, action === 'approve' ? 'admin_approved' : 'admin_rejected', submission.id);
      }
    }
    const published = listPlayerLibrary(db);
    const pendingReconcile = createSubmissionReconciler(published.entities, published.entityRedirects);
    const pendingEvidence = readAutomaticIdentityEvidence(db);
    return { revision: nextRevision, reviewed: submissions.length, acceptedEntityCount, skippedEntityCount: skippedEntities.length,
      skippedEntities, conflicts, pendingSubmissionCount: pendingIds.length,
      pendingSubmissions: pendingIds.map(id => readAdminSubmission(db, id, published.entities, pendingReconcile, pendingEvidence)) };
  }).immediate();
}
