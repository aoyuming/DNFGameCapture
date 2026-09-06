import { serverConfig } from './config.js';
import { createCloudMatchApp } from './app.js';

const host = process.env.HOST ?? '0.0.0.0';
if (!serverConfig.adminPassword) {
  console.error('ADMIN_PASSWORD is required before the server can start.');
  process.exit(1);
}
const app = createCloudMatchApp({
  adminPassword: serverConfig.adminPassword,
  v2ServerUrl: serverConfig.publicUrl,
});

app.httpServer.listen(serverConfig.port, host, () => {
  console.log(`Cloud match server listening on ${host}:${serverConfig.port}`);
});
app.adminHttpServer.listen(serverConfig.adminPort, serverConfig.adminHost, () => {
  console.log(
    `Cloud match admin listening on ${serverConfig.adminHost}:${serverConfig.adminPort}`,
  );
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
