import { createHash } from 'node:crypto';
import type { PlayerEntity } from './player-library.js';

export type ConflictResolutionKind = 'unique_name_target' | 'public_merge' | 'ambiguous';

export interface ResolutionConflict {
  kind: 'names' | 'gameIds';
  value: string;
  entityIds: string[];
}

export interface ResolutionSubmission {
  id: number;
  submissionRevision: string;
  entities: PlayerEntity[];
}

export interface ConflictResolutionSource extends PlayerEntity {
  submissionId: number;
  submissionRevision: string;
}

export interface ConflictResolutionGroup {
  token: string;
  kind: ConflictResolutionKind;
  conflictNames: string[];
  publicEntityIds: string[];
  sources: ConflictResolutionSource[];
  suggestedTargetEntityId?: string;
}

const compareStrings = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

const compareSources = (left: ConflictResolutionSource, right: ConflictResolutionSource): number =>
  left.submissionId - right.submissionId ||
  compareStrings(left.entityId, right.entityId) ||
  compareStrings(left.submissionRevision, right.submissionRevision) ||
  compareStrings(JSON.stringify(left.names), JSON.stringify(right.names)) ||
  compareStrings(JSON.stringify(left.gameIds), JSON.stringify(right.gameIds));

export function buildConflictResolutionGroups(input: {
  revision: number;
  publicEntities: PlayerEntity[];
  submissions: ResolutionSubmission[];
  conflicts: ResolutionConflict[];
}): ConflictResolutionGroup[] {
  const publicIds = new Set(input.publicEntities.map(entity => entity.entityId));
  const sourcesByEntityId = new Map<string, ConflictResolutionSource[]>();
  for (const submission of input.submissions) {
    for (const entity of submission.entities) {
      const sources = sourcesByEntityId.get(entity.entityId) ?? [];
      sources.push({
        entityId: entity.entityId,
        names: [...entity.names],
        gameIds: [...entity.gameIds],
        submissionId: submission.id,
        submissionRevision: submission.submissionRevision,
      });
      sourcesByEntityId.set(entity.entityId, sources);
    }
  }

  const conflicts = input.conflicts
    .map(conflict => ({ ...conflict, entityIds: [...new Set(conflict.entityIds)].sort(compareStrings) }))
    .filter(conflict => conflict.entityIds.length >= 2);
  const neighbors = new Map<string, Set<string>>();
  const connect = (left: string, right: string): void => {
    const connected = neighbors.get(left) ?? new Set<string>();
    connected.add(right);
    neighbors.set(left, connected);
  };
  for (const conflict of conflicts) {
    const first = conflict.entityIds[0];
    for (const entityId of conflict.entityIds.slice(1)) {
      connect(first, entityId);
      connect(entityId, first);
    }
  }

  const seen = new Set<string>();
  const groups: ConflictResolutionGroup[] = [];
  for (const start of [...neighbors.keys()].sort(compareStrings)) {
    if (seen.has(start)) continue;
    seen.add(start);
    const pending = [start];
    const component: string[] = [];
    while (pending.length > 0) {
      const entityId = pending.pop()!;
      component.push(entityId);
      for (const neighbor of [...(neighbors.get(entityId) ?? [])].sort(compareStrings)) {
        if (seen.has(neighbor)) continue;
        seen.add(neighbor);
        pending.push(neighbor);
      }
    }

    component.sort(compareStrings);
    const memberIds = new Set(component);
    const componentConflicts = conflicts.filter(conflict =>
      conflict.entityIds.some(entityId => memberIds.has(entityId)));
    const conflictNames = [...new Set(componentConflicts
      .filter(conflict => conflict.kind === 'names')
      .map(conflict => conflict.value))].sort(compareStrings);
    const publicEntityIds = component.filter(entityId => publicIds.has(entityId));
    const sources = component
      .flatMap(entityId => sourcesByEntityId.get(entityId) ?? [])
      .sort(compareSources);
    const hasNonTargetSource = publicEntityIds.length === 1 &&
      sources.some(source => source.entityId !== publicEntityIds[0]);
    const hasAuthoritativeNameEvidence = componentConflicts.some(conflict => conflict.kind === 'names');
    const kind: ConflictResolutionKind = publicEntityIds.length >= 2
      ? 'public_merge'
      : hasNonTargetSource && hasAuthoritativeNameEvidence
        ? 'unique_name_target'
        : 'ambiguous';
    const signed = {
      revision: input.revision,
      kind,
      entityIds: component,
      publicEntityIds,
      conflictNames,
      sources: sources.map(source => ({
        submissionId: source.submissionId,
        submissionRevision: source.submissionRevision,
        entityId: source.entityId,
      })),
    };
    groups.push({
      token: createHash('sha256').update(JSON.stringify(signed)).digest('hex'),
      kind,
      conflictNames,
      publicEntityIds,
      sources,
      ...(kind === 'unique_name_target'
        ? { suggestedTargetEntityId: publicEntityIds[0] }
        : {}),
    });
  }

  return groups.sort((left, right) => compareStrings(left.token, right.token));
}
