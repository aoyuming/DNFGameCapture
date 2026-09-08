import express, { type NextFunction, type Request, type Response } from 'express';
import type Database from 'better-sqlite3';
import { z } from 'zod';
import { listPlayerLibrary, readAutomaticIdentityEvidence } from './library-store.js';
import {
  adminEntitySchema, editAdminSubmission, entityIdSchema, LIBRARY_ADMIN_MAX_BYTES, LibraryAdminError,
  listAdminSubmissions, mergePublicLibrary, mutatePublicLibrary, parseAdminEntities, parseLibraryImport, projectedLibrary,
  readAdminSubmission, requireLibraryRevision, reviewAdminSubmissions, revisionSchema,
} from './library-admin-data.js';

const guarded = z.object({ revision: revisionSchema }).strict();
const writeEntity = guarded.extend({ entity: adminEntitySchema });
const importBody = guarded.extend({ text: z.string().min(1) });
const submissionRef = z.object({ id: z.number().int().positive().max(Number.MAX_SAFE_INTEGER), submissionRevision: z.string().regex(/^[a-f0-9]{64}$/) }).strict();
const mergeBody = guarded.extend({ targetEntityId: entityIdSchema, entityIds: z.array(entityIdSchema).min(2).max(100), confirm: z.literal(true) })
  .refine(body => new Set(body.entityIds).size === body.entityIds.length && body.entityIds.includes(body.targetEntityId));
const reviewBody = guarded.extend({ action: z.enum(['approve', 'reject']), submissions: z.array(submissionRef).min(1).max(100), skipConflicts: z.boolean().optional() })
  .refine(body => new Set(body.submissions.map(ref => ref.id)).size === body.submissions.length);
const editBody = guarded.extend({ submissionRevision: submissionRef.shape.submissionRevision, entities: z.array(adminEntitySchema).min(1).max(10_000),
  confirmReconciliations: z.boolean().optional() });
function parse<T>(schema: z.ZodType<T>, body: unknown): T {
  const parsed = schema.safeParse(body);
  if (!parsed.success) throw new LibraryAdminError(400, 'invalid_request');
  return parsed.data;
}
const queryText = (value: unknown) => typeof value === 'string' ? value.slice(0, 128).normalize('NFC').trim().toLocaleLowerCase() : '';
const matches = (entity: { entityId: string; names: string[]; gameIds: string[] }, query: string) =>
  [entity.entityId, ...entity.names, ...entity.gameIds].some(value => value.toLocaleLowerCase().includes(query));
function submissionId(value: unknown): number {
  if (typeof value !== 'string' || !/^[1-9][0-9]*$/.test(value) || !Number.isSafeInteger(Number(value))) throw new LibraryAdminError(400, 'invalid_request');
  return Number(value);
}
export function createLibraryAdminApi(db: Database.Database, now: () => number) {
  const router = express.Router();
  router.use(express.json({ limit: `${LIBRARY_ADMIN_MAX_BYTES + 32768}b` }));
  router.get('/state', (request, response) => {
    const result = db.transaction(() => {
      const library = listPlayerLibrary(db);
      const submissions = listAdminSubmissions(db, library.entities);
      const q = queryText(request.query.q); const pendingQ = queryText(request.query.pendingQ);
      const filter = request.query.filter;
      return { revision: library.revision, entityRedirects: library.entityRedirects, entities: library.entities.filter(entity => matches(entity, q)),
        submissions: submissions.filter(item => (!pendingQ || item.deviceId.toLocaleLowerCase().includes(pendingQ) || item.entities.some(entity => matches(entity, pendingQ))) &&
          (filter === 'conflict' ? !item.valid || item.conflicts.length > 0 : filter === 'clean' ? item.valid && !item.conflicts.length : true)),
        stats: { pending: submissions.length, conflicts: submissions.filter(item => !item.valid || item.conflicts.length > 0).length,
          pendingGameIds: submissions.reduce((total, item) => total + item.entities.reduce((sum, entity) => sum + entity.gameIds.length, 0), 0),
          entities: library.entities.length, gameIds: library.entities.reduce((sum, entity) => sum + entity.gameIds.length, 0) } };
    })();
    response.json({ ok: true, ...result });
  });
  router.get('/entities/:entityId', (request, response) => {
    const id = parse(entityIdSchema, request.params.entityId);
    const result = db.transaction(() => {
      const library = listPlayerLibrary(db); const entity = library.entities.find(entity => entity.entityId === id);
      if (!entity) throw new LibraryAdminError(404, 'entity_not_found');
      return { revision: library.revision, entity };
    })();
    response.json({ ok: true, ...result });
  });
  router.post('/entities', (request, response) => {
    const body = parse(writeEntity, request.body);
    response.status(201).json({ ok: true, ...mutatePublicLibrary(db, body.revision, now(), 'create', parseAdminEntities({ entities: [body.entity] })) });
  });
  router.post('/entities/merge', (request, response) => {
    const body = parse(mergeBody, request.body);
    response.json({ ok: true, ...mergePublicLibrary(db, body.revision, now(), body.targetEntityId, body.entityIds) });
  });
  router.put('/entities/:entityId', (request, response) => {
    const id = parse(entityIdSchema, request.params.entityId); const body = parse(writeEntity, request.body);
    if (body.entity.entityId && body.entity.entityId !== id) throw new LibraryAdminError(400, 'entity_id_mismatch');
    response.json({ ok: true, ...mutatePublicLibrary(db, body.revision, now(), 'replace', parseAdminEntities({ entities: [{ ...body.entity, entityId: id }] }), id) });
  });
  router.delete('/entities/:entityId', (request, response) => {
    const id = parse(entityIdSchema, request.params.entityId); const body = parse(guarded, request.body);
    response.json({ ok: true, ...mutatePublicLibrary(db, body.revision, now(), 'delete', [], id) });
  });
  router.post('/import/preview', (request, response) => {
    const body = parse(importBody, request.body); const entities = parseLibraryImport(body.text);
    const result = db.transaction(() => {
      const library = requireLibraryRevision(db, body.revision);
      const projection = projectedLibrary(library.entities, entities, readAutomaticIdentityEvidence(db));
      return { revision: library.revision, entities, conflicts: projection.conflicts, automaticGroups: projection.automaticGroups };
    })();
    response.json({ ok: true, ...result });
  });
  router.post('/import', (request, response) => {
    const body = parse(importBody, request.body);
    response.json({ ok: true, ...mutatePublicLibrary(db, body.revision, now(), 'import', parseLibraryImport(body.text)) });
  });
  router.get('/submissions/:submissionId', (request, response) => {
    response.json({ ok: true, ...db.transaction(() => ({ revision: listPlayerLibrary(db).revision, submission: readAdminSubmission(db, submissionId(request.params.submissionId)) }))() });
  });
  router.put('/submissions/:submissionId', (request, response) => {
    const body = parse(editBody, request.body);
    response.json({ ok: true, ...editAdminSubmission(db, body.revision, submissionId(request.params.submissionId), body.submissionRevision,
      parseAdminEntities({ entities: body.entities }), body.confirmReconciliations) });
  });
  router.post('/review', (request, response) => {
    const body = parse(reviewBody, request.body);
    response.json({ ok: true, ...reviewAdminSubmissions(db, body.revision, now(), body.action, body.submissions, body.skipConflicts) });
  });
  router.use((error: unknown, _request: Request, response: Response, _next: NextFunction) => {
    if (error instanceof LibraryAdminError) { response.status(error.status).json({ ok: false, code: error.code, conflicts: error.conflicts }); return; }
    const type = (error as { type?: string })?.type;
    if (type === 'entity.too.large') { response.status(413).json({ ok: false, code: 'library_too_large' }); return; }
    if (type === 'entity.parse.failed') { response.status(400).json({ ok: false, code: 'invalid_json' }); return; }
    response.status(500).json({ ok: false, code: 'internal_error' });
  });
  return router;
}
