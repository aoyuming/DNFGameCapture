import { canonicalizeIdentifiers, type PlayerEntity } from './player-library.js';
import { canonicalEntityRedirects, type EntityRedirect } from './library-store.js';

const fields = ['names', 'gameIds'] as const;
type Field = typeof fields[number];
type Values = Record<Field, string[]>;
type Keys = Record<Field, Set<string>>;
const key = (value: string) => value.normalize('NFC').trim().toLocaleLowerCase();
const values = (): Values => ({ names: [], gameIds: [] });
const keys = (entity: PlayerEntity): Keys => ({
  names: new Set(entity.names.map(key)), gameIds: new Set(entity.gameIds.map(key)),
});

export interface SubmissionReconciliation {
  submittedEntityId: string;
  entityId: string;
  reason: 'entity_redirect';
}

/** Resolve only durable redirects; inference runs once on the complete publication projection. */
export function createSubmissionReconciler(existing: readonly PlayerEntity[], entityRedirects: readonly EntityRedirect[] = []) {
  const redirects = new Map(canonicalEntityRedirects(existing, entityRedirects).map(item => [item.fromEntityId, item.toEntityId]));
  const byId = new Map(existing.map(entity => [entity.entityId, keys(entity)]));

  return (incoming: readonly PlayerEntity[]) => {
    const reconciliations: SubmissionReconciliation[] = [];
    const entityTargets: string[] = [];
    const merged = new Map<string, PlayerEntity>();
    const mergedKeys = new Map<string, Keys>();
    for (const source of incoming) {
      const entity: PlayerEntity = { entityId: source.entityId, names: canonicalizeIdentifiers(source.names),
        gameIds: canonicalizeIdentifiers(source.gameIds) };
      const evidence = keys(entity);
      const redirected = redirects.get(entity.entityId);
      if (redirected) {
        reconciliations.push({ submittedEntityId: entity.entityId, entityId: redirected, reason: 'entity_redirect' });
        entity.entityId = redirected;
      }
      entityTargets.push(entity.entityId);
      const previous = merged.get(entity.entityId);
      if (!previous) {
        merged.set(entity.entityId, entity);
        mergedKeys.set(entity.entityId, evidence);
      } else {
        const seen = mergedKeys.get(entity.entityId)!;
        for (const field of fields) for (const value of entity[field]) {
          const normalized = key(value);
          if (!seen[field].has(normalized)) {
            seen[field].add(normalized);
            previous[field].push(value);
          }
        }
      }
    }
    const entities = [...merged.values()];
    const additions = { names: 0, gameIds: 0 };
    let unchangedCount = 0; let addedEntityCount = 0; let updatedEntityCount = 0;
    const entityChanges = entities.map(entity => {
      const previous = byId.get(entity.entityId);
      const added = values();
      for (const field of fields) {
        added[field] = entity[field].filter(value => !previous?.[field].has(key(value)));
        additions[field] += added[field].length;
      }
      const status = !previous ? 'added' : fields.some(field => added[field].length > 0) ? 'updated' : 'unchanged';
      if (status === 'added') addedEntityCount++;
      else if (status === 'updated') updatedEntityCount++;
      else unchangedCount++;
      return { entityId: entity.entityId, status, additions: added };
    });
    return { entities, entityTargets, entityChanges, reconciliations, rawEntityCount: incoming.length,
      matchedEntityCount: reconciliations.length, unchangedCount, addedEntityCount, updatedEntityCount, additions };
  };
}

export function reconcileLibrarySubmission(existing: readonly PlayerEntity[], incoming: readonly PlayerEntity[], entityRedirects: readonly EntityRedirect[] = []) {
  return createSubmissionReconciler(existing, entityRedirects)(incoming);
}
