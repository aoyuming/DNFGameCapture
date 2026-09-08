# SQLite Amalgamation

SQLite 3.49.2, source ID:
`2025-05-07 10:39:52 17144570b0d96ae63cd6f3edca39e27ebd74925252bbaf6723bcb2f6b4861fb1`.

Vendored unchanged from this workspace's
`cloud-match-server/node_modules/better-sqlite3/deps/sqlite3/`.
Upstream: https://sqlite.org/2025/sqlite-amalgamation-3490200.zip
License: public domain (https://sqlite.org/copyright.html).

SHA-256:
- sqlite3.c: E80215754AC6CFAEFE272342EFD581B6DBEFDBDC21F96BD83FCB698BEE9D36A5
- sqlite3.h: 2198C66D4C59AE86421590C27FD53309EC14A595AE31C0C7143247444018A0BD

Compile sqlite3.c as C, with precompiled headers disabled, and link its object
statically. The core test script uses SQLITE_THREADSAFE=1 and SQLITE_DQS=0.
No SQLite DLL is needed. Do not substitute the plan's older 3.46.1 version.
