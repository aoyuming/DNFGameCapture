import type Database from 'better-sqlite3';
import { z } from 'zod';
import { MAX_PLAYER_ENTITY_VALUES, type PlayerEntity as PublicPlayerEntity } from './player-library.js';

export interface EntityRedirect { fromEntityId: string; toEntityId: string }
export const MAX_ENTITY_REDIRECTS = 10_000;
export const MAX_AUTOMATIC_EVIDENCE_BYTES = 1024 * 1024;
const evidenceValues = z.array(z.string().min(1).max(128)).max(MAX_PLAYER_ENTITY_VALUES);
const automaticEvidenceSchema = z.array(z.object({
  entityId: z.string().regex(/^[A-Za-z0-9_-]{1,128}$/),
  names: evidenceValues.min(1), gameIds: evidenceValues,
}).strip()).min(1).max(MAX_PLAYER_ENTITY_VALUES);

export function readAutomaticIdentityEvidence(db: Database.Database): Map<string, readonly PublicPlayerEntity[]> {
  const totals = db.prepare(`SELECT COUNT(*) AS count,COALESCE(SUM(length(CAST(evidence_json AS BLOB))),0) AS bytes
    FROM player_entity_auto_evidence`).get() as { count: number; bytes: number };
  if (totals.count > MAX_PLAYER_ENTITY_VALUES || totals.bytes > MAX_AUTOMATIC_EVIDENCE_BYTES) {
    throw new Error('invalid_automatic_identity_evidence');
  }
  const rows = db.prepare('SELECT entity_id,evidence_json FROM player_entity_auto_evidence ORDER BY entity_id')
    .all() as { entity_id: string; evidence_json: string }[];
  const result = new Map<string, readonly PublicPlayerEntity[]>();
  for (const row of rows) {
    let parsed: unknown;
    try { parsed = JSON.parse(row.evidence_json); } catch { throw new Error('invalid_automatic_identity_evidence'); }
    const evidence = automaticEvidenceSchema.safeParse(parsed);
    if (!evidence.success) throw new Error('invalid_automatic_identity_evidence');
    result.set(row.entity_id, evidence.data);
  }
  return result;
}

export function canonicalEntityRedirects(entities: readonly PublicPlayerEntity[], redirects: readonly EntityRedirect[]): EntityRedirect[] {
  if (redirects.length > MAX_ENTITY_REDIRECTS) throw new Error('invalid_entity_redirects');
  const live = new Set(entities.map(entity => entity.entityId));
  const targets = new Map<string, string>();
  for (const item of redirects) {
    if (![item.fromEntityId, item.toEntityId].every(id => typeof id === 'string' && /^[A-Za-z0-9_-]{1,128}$/.test(id)) ||
      live.has(item.fromEntityId) || targets.has(item.fromEntityId)) throw new Error('invalid_entity_redirects');
    targets.set(item.fromEntityId, item.toEntityId);
  }
  // Memoize paths so a long chain is validated and flattened in linear time.
  const resolved = new Map<string, string>();
  for (const source of targets.keys()) {
    const path = new Set<string>(); let target = source;
    while (targets.has(target) && !resolved.has(target)) {
      if (path.has(target)) throw new Error('invalid_entity_redirects');
      path.add(target); target = targets.get(target)!;
    }
    target = resolved.get(target) ?? target;
    if (!live.has(target)) throw new Error('invalid_entity_redirects');
    for (const id of path) resolved.set(id, target);
  }
  return [...resolved].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)
    .map(([fromEntityId, toEntityId]) => ({ fromEntityId, toEntityId }));
}

export function readEntityRedirects(db: Database.Database, entities: readonly PublicPlayerEntity[]): EntityRedirect[] {
  const redirects = db.prepare('SELECT from_entity_id AS fromEntityId,to_entity_id AS toEntityId FROM player_entity_redirects LIMIT ?')
    .all(MAX_ENTITY_REDIRECTS + 1) as EntityRedirect[];
  return canonicalEntityRedirects(entities, redirects);
}

function readLibraryEntities(db: Database.Database): PublicPlayerEntity[] {
  const rows = db.prepare(
    'SELECT entity_id, created_at, updated_at FROM player_entities ORDER BY entity_id',
  ).all() as Array<{ entity_id: string; created_at: number; updated_at: number }>;
  const names = db.prepare(
    'SELECT entity_id, display_name FROM player_entity_names ORDER BY entity_id, name_norm',
  ).all() as Array<{ entity_id: string; display_name: string }>;
  const identifiers = db.prepare(
    `SELECT link.entity_id, identifier.kind, spelling.display_value
     FROM player_entity_identifiers AS link
     JOIN player_identifiers AS identifier ON identifier.identifier_id = link.identifier_id
     JOIN player_identifier_spellings AS spelling
       ON spelling.identifier_id = link.identifier_id AND spelling.spelling_id = link.spelling_id
     WHERE identifier.kind = 'game'
     ORDER BY link.entity_id, identifier.identifier_norm`,
  ).all() as Array<{ entity_id: string; kind: 'game'; display_value: string }>;
  const byId = new Map<string, PublicPlayerEntity>();
  for (const row of rows) {
    byId.set(row.entity_id, {
      entityId: row.entity_id,
      names: [],
      gameIds: [],
    });
  }
  for (const row of names) byId.get(row.entity_id)?.names.push(row.display_name);
  for (const row of identifiers) {
    const entity = byId.get(row.entity_id);
    if (!entity) continue;
    entity.gameIds.push(row.display_value);
  }
  return [...byId.values()];
}

export function listPlayerLibrary(db: Database.Database): {
  revision: number;
  entities: PublicPlayerEntity[];
  entityRedirects: EntityRedirect[];
} {
  const revision = (db.prepare(
    'SELECT revision FROM player_library_meta WHERE id = 1',
  ).get() as { revision: number } | undefined)?.revision ?? 0;
  const entities = readLibraryEntities(db);
  return { revision, entities, entityRedirects: readEntityRedirects(db, entities) };
}
