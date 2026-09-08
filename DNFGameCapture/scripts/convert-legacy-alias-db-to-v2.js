#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [inputPath, outputPath, reportPathArg] = process.argv.slice(2);

if (!inputPath || !outputPath) {
  console.error('Usage: node scripts/convert-legacy-alias-db-to-v2.js <input.ini> <output.json> [report.json]');
  process.exit(2);
}

const reportPath = reportPathArg || outputPath.replace(/\.json$/iu, '.report.json');

function cleanText(value) {
  return String(value ?? '')
    .normalize('NFC')
    .replace(/[\r\n\t]/gu, ' ')
    .replace(/[=|]/gu, '')
    .trim();
}

function keyOf(value) {
  return cleanText(value).toLocaleLowerCase();
}

function parseIds(value) {
  const parenthesized = [];
  const pattern = /[（(]([^）)]*)[）)]/gu;
  let match;
  while ((match = pattern.exec(value)) !== null) {
    const id = cleanText(match[1]);
    if (id) parenthesized.push(id);
  }
  if (parenthesized.length > 0) return parenthesized;
  return value
    .split(/[，,；;]/u)
    .map(cleanText)
    .filter(Boolean);
}

function stableEntityId(names) {
  const material = names.map(keyOf).sort().join('\u0000');
  return `player-${createHash('sha256').update(`legacy-alias-v1\u0000${material}`, 'utf8').digest('hex').slice(0, 24)}`;
}

function baseId(value) {
  const index = Math.min(...['#', '＃']
    .map((marker) => {
      const found = value.indexOf(marker);
      return found < 0 ? Number.POSITIVE_INFINITY : found;
    }));
  return cleanText(index === Number.POSITIVE_INFINITY ? value : value.slice(0, index));
}

const source = readFileSync(inputPath, 'utf8').replace(/^\uFEFF/u, '');
const rows = [];
const parseErrors = [];

for (const [lineIndex, rawLine] of source.split(/\r?\n/u).entries()) {
  const lineNumber = lineIndex + 1;
  const line = rawLine.trim();
  if (!line || line.startsWith(';') || line.startsWith('//')) continue;
  const separator = line.indexOf('=');
  if (separator <= 0) {
    parseErrors.push({ line: lineNumber, value: rawLine, reason: 'missing_name_separator' });
    continue;
  }
  const name = cleanText(line.slice(0, separator));
  const ids = parseIds(line.slice(separator + 1));
  if (!name) {
    parseErrors.push({ line: lineNumber, value: rawLine, reason: 'empty_name' });
    continue;
  }
  rows.push({ line: lineNumber, name, ids });
}

const nameIndexByKey = new Map();
const names = [];
for (const row of rows) {
  const nameKey = keyOf(row.name);
  if (!nameIndexByKey.has(nameKey)) {
    nameIndexByKey.set(nameKey, names.length);
    names.push({ key: nameKey, display: row.name });
  }
}

const idOccurrences = new Map();
for (const row of rows) {
  for (const id of row.ids) {
    const idKey = keyOf(id);
    const occurrences = idOccurrences.get(idKey) || [];
    occurrences.push({ name: row.name, id });
    idOccurrences.set(idKey, occurrences);
  }
}

const grouped = new Map();
for (const row of rows) {
  const owner = nameIndexByKey.get(keyOf(row.name));
  // Preserve source ownership; the importing library applies its grouping policy.
  const root = owner;
  let target = grouped.get(root);
  if (!target) {
    target = {
      firstLine: row.line,
      names: [],
      nameKeys: new Set(),
      gameIds: [],
      gameIdKeys: new Set(),
    };
    grouped.set(root, target);
  }
  const nameKey = keyOf(row.name);
  if (!target.nameKeys.has(nameKey)) {
    target.nameKeys.add(nameKey);
    target.names.push(row.name);
  }
  for (const id of row.ids) {
    const idKey = keyOf(id);
    if (target.gameIdKeys.has(idKey)) continue;
    target.gameIdKeys.add(idKey);
    target.gameIds.push(id);
  }
}

const entities = [...grouped.values()]
  .sort((left, right) => left.firstLine - right.firstLine)
  .map((group) => ({
    entityId: stableEntityId(group.names),
    names: group.names,
    gameIds: group.gameIds,
  }));

const duplicateIdGroups = [...idOccurrences.entries()]
  .filter(([, occurrences]) => occurrences.length > 1)
  .map(([idKey, occurrences]) => ({
    normalizedId: idKey,
    occurrences: occurrences.length,
    names: [...new Set(occurrences.map((item) => item.name))],
    spellings: [...new Set(occurrences.map((item) => item.id))],
  }))
  .sort((left, right) => right.occurrences - left.occurrences || left.normalizedId.localeCompare(right.normalizedId));

const baseIdGroups = new Map();
for (const [idKey, occurrences] of idOccurrences) {
  const spelling = occurrences[0]?.id || idKey;
  const baseKey = keyOf(baseId(spelling));
  const entry = baseIdGroups.get(baseKey) || { spellings: new Set(), names: new Set() };
  for (const occurrence of occurrences) {
    entry.spellings.add(occurrence.id);
    entry.names.add(occurrence.name);
  }
  baseIdGroups.set(baseKey, entry);
}
const baseIdVariants = [...baseIdGroups.entries()]
  .filter(([, value]) => value.spellings.size > 1)
  .map(([normalizedBaseId, value]) => ({
    normalizedBaseId,
    spellings: [...value.spellings],
    names: [...value.names],
  }));

const output = { entities };
const report = {
  source: path.resolve(inputPath),
  output: path.resolve(outputPath),
  sourceNameCount: names.length,
  sourceRowCount: rows.length,
  sourceGameIdOccurrenceCount: [...idOccurrences.values()].reduce((sum, items) => sum + items.length, 0),
  uniqueGameIdCount: idOccurrences.size,
  duplicateOccurrenceCount: [...idOccurrences.values()]
    .reduce((sum, items) => sum + Math.max(0, items.length - 1), 0),
  outputEntityCount: entities.length,
  mergedNameGroupCount: entities.filter((entity) => entity.names.length > 1).length,
  parseErrors,
  duplicateIdGroups,
  baseIdVariants,
  note: 'gameIds keep the original #job suffix; name-only entities are retained.',
};

writeFileSync(outputPath, `${JSON.stringify(output, null, 2)}\n`, 'utf8');
writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');

console.log(JSON.stringify({
  output: path.resolve(outputPath),
  report: path.resolve(reportPath),
  sourceNameCount: report.sourceNameCount,
  sourceGameIdOccurrenceCount: report.sourceGameIdOccurrenceCount,
  uniqueGameIdCount: report.uniqueGameIdCount,
  outputEntityCount: report.outputEntityCount,
  mergedNameGroupCount: report.mergedNameGroupCount,
  duplicateOccurrenceCount: report.duplicateOccurrenceCount,
  parseErrorCount: report.parseErrors.length,
  baseIdVariantCount: report.baseIdVariants.length,
}, null, 2));
