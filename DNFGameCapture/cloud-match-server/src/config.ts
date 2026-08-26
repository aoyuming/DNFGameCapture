export interface ServerConfig {
  port: number;
  databasePath: string;
  snapshotMaxBytes: number;
  broadcasterMaxLength: number;
  staleThresholdSeconds: number;
  excludedFromConsensusThresholdSeconds: number;
}

export const serverConfig: ServerConfig = {
  port: 18880,
  databasePath: process.env.DATABASE_PATH ?? 'cloud-match.sqlite',
  snapshotMaxBytes: 65_536,
  broadcasterMaxLength: 32,
  staleThresholdSeconds: 30,
  excludedFromConsensusThresholdSeconds: 120,
};
