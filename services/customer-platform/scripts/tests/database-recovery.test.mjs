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
      await readFile(resolve(root, 'migrations/0002_operations.sql'), 'utf8')+'\n'+
      await readFile(resolve(root,'migrations/0003_accounts_vip.sql'),'utf8')+'\n'+
      await readFile(resolve(root,'migrations/0004_paddle.sql'),'utf8');
    const seed = resolve(folder, 'seed.sql');
    await writeFile(seed, sql + `\nINSERT INTO customers VALUES ('customer','provider',NULL,1);\nINSERT INTO licenses (id,customer_id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,checked_at,blocked) VALUES ('license','customer','order','item','variant','studio','perpetual',0,'["editor"]',2,'active',1,1);\nINSERT INTO installations(id,credential_hash,customer_id,license_id,trial_ends_at,revoked,created_at,last_seen_at,app_version,platform) VALUES ('revoked','credential-hash','customer','license',1,1,1,1,'0.1.0','mac-arm');\nINSERT INTO staff VALUES ('operator','admin');\nINSERT INTO audit VALUES ('record','operator','revoke','revoked','synthetic recovery evidence',1);\nINSERT INTO privacy_tombstones VALUES ('customer','provider-hash',1);\n`);
    await execute(process.execPath, [resolve(root, 'node_modules/wrangler/bin/wrangler.js'), 'd1', 'execute', 'DB', '--config', config, '--local', '--file', seed], { cwd: folder, env: { ...process.env, CI: 'true', WRANGLER_LOG_PATH: resolve(folder, 'seed.log') } });
    const newSeed=resolve(folder,'accounts.sql');
    await writeFile(newSeed,`INSERT INTO accounts VALUES('account','owner@recovery.test',1,1,0);
      UPDATE installations SET account_id='account',account_session_expires_at=2592001;
      INSERT INTO account_licenses VALUES('license','account',1);
      INSERT INTO email_challenges VALUES('challenge','revoked','owner@recovery.test','hashed-code',600,5,1,1);
      INSERT INTO vip_codes VALUES('vip','hashed-vip','VIP','["editor"]',0,2,1,100,100,0,1);
      INSERT INTO vip_redemptions VALUES('vip','account','license',1);
      UPDATE vip_codes SET revoked=1;
      DELETE FROM privacy_tombstones;
      INSERT INTO paddle_accounts VALUES('account','ctm-recovery','sandbox');
      INSERT INTO paddle_checkouts VALUES('txn-recovery','account','ctm-recovery','pri-recovery','sandbox',1,2,3);
      INSERT INTO paddle_licenses VALUES('license','account','ctm-recovery','txn-recovery',NULL,'sandbox');
      INSERT INTO privacy_tombstones VALUES('customer','provider-hash',1);
    `);
    await execute(process.execPath,[resolve(root,'node_modules/wrangler/bin/wrangler.js'),'d1','execute','DB','--config',config,'--local','--file',newSeed],{cwd:folder,env:{...process.env,CI:'true',WRANGLER_LOG_PATH:resolve(folder,'seed.log')}});
    const backup = resolve(folder, 'backup');
    const restore = resolve(folder, 'restore');
    const result = await backupDatabase({ config, directory: backup, mode: 'local' });
    assert.equal(result.tableCount, 18);
    assert.equal((await stat(backup)).mode & 0o777, 0o700);
    assert.equal((await stat(resolve(backup, 'database.sql'))).mode & 0o777, 0o600);
    assert.equal((await rehearseRestore({ backup, directory: restore })).passed, true);
    const report = JSON.parse(await readFile(resolve(restore, 'restore-report.json'), 'utf8'));
    assert.equal(report.snapshot.counts.installations, 1);
    assert.equal(report.snapshot.counts.audit, 1);
    assert.equal(report.snapshot.counts.privacy_tombstones, 1);
    for(const table of ['accounts','account_licenses','email_challenges','vip_codes','vip_redemptions','paddle_accounts','paddle_checkouts','paddle_licenses']) assert.equal(report.snapshot.counts[table],1);
    await assert.rejects(backupDatabase({ config, directory: backup, mode: 'local' }), { code: 'EEXIST' });
    await assert.rejects(rehearseRestore({ backup, directory: restore }), { code: 'EEXIST' });
    await writeFile(resolve(backup, 'database.sql'), 'SELECT 1;');
    await assert.rejects(rehearseRestore({ backup, directory: resolve(folder, 'corrupt-restore') }), /checksum/);
  } finally { if(process.env.LOOKSY_KEEP_RECOVERY_EVIDENCE!=='1') await rm(folder, { recursive: true, force: true }); }
});
