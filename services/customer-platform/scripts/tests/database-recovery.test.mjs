import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdtemp, readFile, writeFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { backupDatabase, rehearseRestore, runCli } from '../database-recovery.mjs';

test('recovery requires explicit source mode and refuses missing/corrupt backups', async () => {
  await assert.rejects(backupDatabase({ config: 'missing', directory: 'missing' }), /Select/);
  const folder = await mkdtemp(resolve(tmpdir(), 'looksy-corrupt-backup-'));
  try {
    await writeFile(resolve(folder, 'database.sql'), 'SELECT 1;');
    await writeFile(resolve(folder, 'manifest.json'), JSON.stringify({ schema: 1, sha256: '0'.repeat(64) }));
    await assert.rejects(rehearseRestore({ backup: folder, directory: resolve(folder, 'should-not-exist') }), /checksum/);
    await assert.rejects(stat(resolve(folder, 'should-not-exist')), { code: 'ENOENT' });
  } finally { await rm(folder, { recursive: true }); }
});
test('recovery CLI rejects production restore and ambiguous source flags', async () => {
  let output = '';
  const streams = { stdout: { write(text) { output += text; } }, stderr: { write(text) { output += text; } } };
  assert.equal(await runCli(['rehearse', '--backup', '/a', '--directory', '/b', '--remote'], streams), 2);
  assert.equal(await runCli(['backup', '--config', '/a', '--directory', '/b', '--local', '--remote'], streams), 2);
  assert.match(output, /Unknown option/);
  assert.match(output, /only one/);
});

test('D1 backup and isolated restore preserve every row, revoked credentials and audit records', { skip: process.env.LOOKSY_RUN_D1_RECOVERY_TEST !== '1', timeout: 180000 }, async () => {
  const execute = promisify(execFile);
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
  const folder = await mkdtemp(resolve(tmpdir(), 'looksy-d1-recovery-'));
  const config = resolve(folder, 'wrangler.json');
  try {
    await writeFile(config, JSON.stringify({ name: 'looksy-recovery-test', compatibility_date: '2026-09-23', d1_databases: [{ binding: 'DB', database_name: 'looksy-recovery-test', database_id: '00000000-0000-0000-0000-000000000002' }] }));
    const sql = await readFile(resolve(root, 'migrations/0001_customers.sql'), 'utf8') + '\n' +
      await readFile(resolve(root, 'migrations/0002_operations.sql'), 'utf8');
    const seed = resolve(folder, 'seed.sql');
    await writeFile(seed, sql + `\nINSERT INTO customers VALUES ('customer','provider',NULL,1);\nINSERT INTO licenses (id,customer_id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,checked_at,blocked) VALUES ('license','customer','order','item','variant','studio','perpetual',0,'["editor"]',2,'active',1,1);\nINSERT INTO installations VALUES ('revoked','credential-hash','customer','license',1,1,1,1,'0.1.0','mac-arm');\nINSERT INTO staff VALUES ('operator','admin');\nINSERT INTO audit VALUES ('record','operator','revoke','revoked','synthetic recovery evidence',1);\nINSERT INTO privacy_tombstones VALUES ('customer','provider-hash',1);\n`);
    await execute(process.execPath, [resolve(root, 'node_modules/wrangler/bin/wrangler.js'), 'd1', 'execute', 'DB', '--config', config, '--local', '--file', seed], { cwd: folder, env: { ...process.env, CI: 'true', WRANGLER_LOG_PATH: resolve(folder, 'seed.log') } });
    const backup = resolve(folder, 'backup');
    const restore = resolve(folder, 'restore');
    const result = await backupDatabase({ config, directory: backup, mode: 'local' });
    assert.equal(result.tableCount, 10);
    assert.equal((await stat(backup)).mode & 0o777, 0o700);
    assert.equal((await stat(resolve(backup, 'database.sql'))).mode & 0o777, 0o600);
    assert.equal((await rehearseRestore({ backup, directory: restore })).passed, true);
    const report = JSON.parse(await readFile(resolve(restore, 'restore-report.json'), 'utf8'));
    assert.equal(report.snapshot.counts.installations, 1);
    assert.equal(report.snapshot.counts.audit, 1);
    assert.equal(report.snapshot.counts.privacy_tombstones, 1);
    await assert.rejects(backupDatabase({ config, directory: backup, mode: 'local' }), { code: 'EEXIST' });
    await assert.rejects(rehearseRestore({ backup, directory: restore }), { code: 'EEXIST' });
    await writeFile(resolve(backup, 'database.sql'), 'SELECT 1;');
    await assert.rejects(rehearseRestore({ backup, directory: resolve(folder, 'corrupt-restore') }), /checksum/);
  } finally { await rm(folder, { recursive: true, force: true }); }
});
