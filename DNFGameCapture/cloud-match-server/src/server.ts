import { serverConfig } from './config.js';
import { createCloudMatchApp } from './app.js';

const host = process.env.HOST ?? '0.0.0.0';
const app = createCloudMatchApp();

app.httpServer.listen(serverConfig.port, host, () => {
  console.log(`Cloud match server listening on ${host}:${serverConfig.port}`);
});

let shutdownPromise: Promise<void> | undefined;
function shutdown(): Promise<void> {
  shutdownPromise ??= app.close();
  return shutdownPromise;
}

async function handleShutdown(): Promise<void> {
  try {
    await shutdown();
  } catch {
    console.error('Cloud match server shutdown failed');
    process.exitCode = 1;
  }
}

process.once('SIGINT', handleShutdown);
process.once('SIGTERM', handleShutdown);
