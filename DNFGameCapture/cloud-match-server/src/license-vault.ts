import { createCipheriv, createDecipheriv, randomBytes } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import type Database from 'better-sqlite3';
import { hashLicenseKey, normalizeLicenseKey } from './auth.js';

const keys = new WeakMap<Database.Database, Buffer>();
export class LicenseVaultError extends Error { constructor() { super('vault_unavailable'); } }

function vaultKey(db: Database.Database): Buffer {
  const cached = keys.get(db);
  if (cached) return cached;
  try {
    let key: Buffer;
    if (db.name === ':memory:' || db.name === '') key = randomBytes(32);
    else {
      const path = db.name + '.license-key';
      if (!existsSync(path)) {
        if (db.prepare('SELECT 1 FROM licenses WHERE key_ciphertext IS NOT NULL LIMIT 1').get()) throw new LicenseVaultError();
        try { writeFileSync(path, randomBytes(32), { flag: 'wx', mode: 0o600 }); }
        catch (error) { if ((error as NodeJS.ErrnoException).code !== 'EEXIST') throw error; }
      }
      key = readFileSync(path);
    }
    if (key.length !== 32) throw new LicenseVaultError();
    const proof = db.prepare('SELECT key_ciphertext,key_hash FROM licenses WHERE key_ciphertext IS NOT NULL ORDER BY id LIMIT 1').get() as
      { key_ciphertext: string; key_hash: string } | undefined;
    if (proof) decryptWithKey(proof.key_ciphertext, proof.key_hash, key);
    keys.set(db, key);
    return key;
  } catch { throw new LicenseVaultError(); }
}

export function encryptLicenseKey(db: Database.Database, raw: string): string {
  const value = normalizeLicenseKey(raw), iv = randomBytes(12);
  const cipher = createCipheriv('aes-256-gcm', vaultKey(db), iv);
  cipher.setAAD(Buffer.from('dnf-license-v1:' + hashLicenseKey(value)));
  const ciphertext = Buffer.concat([cipher.update(value, 'utf8'), cipher.final()]);
  return ['v1', iv.toString('base64url'), cipher.getAuthTag().toString('base64url'), ciphertext.toString('base64url')].join('.');
}

function decryptWithKey(ciphertext: string, keyHash: string, masterKey: Buffer): string {
  try {
    const parts = ciphertext.split('.');
    if (parts.length !== 4 || parts[0] !== 'v1' || ciphertext.length > 1024) throw new LicenseVaultError();
    const iv = Buffer.from(parts[1], 'base64url'), tag = Buffer.from(parts[2], 'base64url');
    if (iv.length !== 12 || tag.length !== 16) throw new LicenseVaultError();
    const decipher = createDecipheriv('aes-256-gcm', masterKey, iv);
    decipher.setAAD(Buffer.from('dnf-license-v1:' + keyHash)); decipher.setAuthTag(tag);
    const key = Buffer.concat([decipher.update(Buffer.from(parts[3], 'base64url')), decipher.final()]).toString('utf8');
    if (hashLicenseKey(key) !== keyHash) throw new LicenseVaultError();
    return key;
  } catch { throw new LicenseVaultError(); }
}

export function decryptLicenseKey(db: Database.Database, ciphertext: string, keyHash: string): string {
  return decryptWithKey(ciphertext, keyHash, vaultKey(db));
}
