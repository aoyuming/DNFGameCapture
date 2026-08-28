export interface ServerConfig {
  port: number;
  adminHost: string;
  adminPort: number;
  adminPassword: string;
  databasePath: string;
  snapshotMaxBytes: number;
  broadcasterMaxLength: number;
  staleThresholdSeconds: number;
  excludedFromConsensusThresholdSeconds: number;
}

function readPort(name: string, fallback: number): number {
  const value = Number.parseInt(process.env[name] ?? '', 10);
  return Number.isSafeInteger(value) && value >= 1 && value <= 65_535
    ? value
    : fallback;
}

function readAdminHost(): string {
  const value = process.env.ADMIN_HOST?.trim();
  return value === '0.0.0.0' || value === '::1' ? value : '127.0.0.1';
}

export const serverConfig: ServerConfig = {
  port: 18880,
  adminHost: readAdminHost(),
  adminPort: readPort('ADMIN_PORT', 18881),
  adminPassword: process.env.ADMIN_PASSWORD?.trim() ?? '',
  databasePath: process.env.DATABASE_PATH ?? 'cloud-match.sqlite',
  snapshotMaxBytes: 65_536,
  broadcasterMaxLength: 32,
  staleThresholdSeconds: 30,
  excludedFromConsensusThresholdSeconds: 120,
};
