import { serverConfig } from './config.js';
import { createCloudMatchApp } from './app.js';

const host = process.env.HOST ?? '0.0.0.0';
const app = createCloudMatchApp();

app.httpServer.listen(serverConfig.port, host, () => {
  console.log(`Cloud match server listening on ${host}:${serverConfig.port}`);
});

let shuttingDown = false;
async function shutdown(): Promise<void> {
  if (shuttingDown) {
    return;
  }
  shuttingDown = true;
  await app.close();
}

process.once('SIGINT', () => {
  void shutdown();
});
process.once('SIGTERM', () => {
  void shutdown();
});
