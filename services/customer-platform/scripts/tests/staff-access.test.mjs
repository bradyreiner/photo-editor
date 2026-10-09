import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { generateGrantSql, generateRevokeSql, runCli } from '../staff-access.mjs';

const base = { subject: 'access-subject-123', actor: 'operator-subject-456', reason: 'Initial enrollment', createdAt: 1_800_000_000 };

test('grant SQL is audited and will not silently replace an existing staff role', () => {
  const sql = generateGrantSql({ ...base, role: 'support' });
  assert.match(sql, /INSERT INTO staff \(subject, role\)/);
  assert.match(sql, /ON CONFLICT\(subject\) DO UPDATE SET role = excluded\.role\s+WHERE staff\.role = excluded\.role;/);
  assert.match(sql, /INSERT INTO audit \(id, actor, action, target, reason, created_at\)/);
  assert.match(sql, /'staff_grant:support'/);
  assert.match(sql, /WHERE changes\(\) > 0;/);
  assert.match(sql, /'operator-subject-456'/);
  assert.match(sql, /'Initial enrollment'/);
  assert.doesNotMatch(sql, /^\s*(?:BEGIN TRANSACTION|COMMIT);/im);
});

test('revoke SQL deletes only the exact subject, audits a deletion, and preserves the last admin', () => {
  const sql = generateRevokeSql({ ...base, createdAt: 1 });
  assert.match(sql, /DELETE FROM staff\s+WHERE subject = 'access-subject-123'/);
  assert.match(sql, /role != 'admin' OR EXISTS \(SELECT 1 FROM staff AS another_admin/);
  assert.match(sql, /another_admin\.subject != 'access-subject-123'/);
  assert.match(sql, /'staff_revoke'/);
  assert.match(sql, /'staff:access-subject-123'/);
  assert.match(sql, /WHERE changes\(\) > 0;/);
});

test('SQL literals escape quote-based injection in identity and audit fields', () => {
  const sql = generateGrantSql({
    subject: "staff'); DROP TABLE staff; --",
    actor: "operator'); DELETE FROM audit; --",
    reason: "quoted'; UPDATE staff SET role='admin'; --",
    role: 'admin',
    createdAt: 12,
  });
  assert.ok(sql.includes("'staff''); DROP TABLE staff; --'"));
  assert.ok(sql.includes("'operator''); DELETE FROM audit; --'"));
  assert.ok(sql.includes("'quoted''; UPDATE staff SET role=''admin''; --'"));
  assert.equal((sql.match(/;\s*$/gm) || []).length, 2, 'only the intended staff and audit statements terminate');
});

test('rejects unsupported roles, malformed values, and invalid timestamps', () => {
  assert.throws(() => generateGrantSql({ ...base, role: 'owner' }), /role must be support or admin/);
  assert.throws(() => generateGrantSql({ ...base, role: "support'; DELETE FROM staff; --" }), /role must be support or admin/);
  assert.throws(() => generateGrantSql({ ...base, subject: 'subject\n-- injected' }), /control characters/);
  assert.throws(() => generateGrantSql({ ...base, actor: ' operator' }), /surrounding whitespace/);
  assert.throws(() => generateRevokeSql({ ...base, reason: '' }), /reason must be/);
  assert.throws(() => generateRevokeSql({ ...base, createdAt: -1 }), /non-negative safe integer/);
});

test('CLI exclusively creates a private SQL file without printing SQL or identity values', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'looksy-staff-access-'));
  try {
    const destination = join(directory, 'grant.sql');
    let out = '';
    let err = '';
    const status = await runCli([
      'grant', '--subject', base.subject, '--role', 'admin', '--actor', base.actor,
      '--reason', base.reason, '--out', destination,
    ], { stdout: { write: value => { out += value; } }, stderr: { write: value => { err += value; } } });

    assert.equal(status, 0, err);
    assert.match(out, /no database was contacted/);
    assert.doesNotMatch(out, /access-subject-123|operator-subject-456|INSERT INTO/);
    assert.equal(err, '');
    const file = await stat(destination);
    assert.equal(file.mode & 0o777, 0o600);
    assert.match(await readFile(destination, 'utf8'), /staff_grant:admin/);

    const secondStatus = await runCli([
      'grant', '--subject', base.subject, '--role', 'admin', '--actor', base.actor,
      '--reason', base.reason, '--out', destination,
    ], { stdout: { write: () => {} }, stderr: { write: value => { err += value; } } });
    assert.equal(secondStatus, 2);
    assert.match(err, /EEXIST/);
    assert.match(await readFile(destination, 'utf8'), /staff_grant:admin/);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test('CLI rejects missing required inputs and unknown options', async () => {
  let output = '';
  let errors = '';
  const missing = await runCli(['grant', '--subject', 'one'], {
    stdout: { write: value => { output += value; } },
    stderr: { write: value => { errors += value; } },
  });
  assert.equal(missing, 2);
  assert.match(errors, /missing required option: --actor/);

  errors = '';
  const unknown = await runCli(['revoke', '--subject', 'one', '--actor', 'two', '--reason', 'why', '--out', '/tmp/nope', '--execute'], {
    stdout: { write: value => { output += value; } },
    stderr: { write: value => { errors += value; } },
  });
  assert.equal(unknown, 2);
  assert.match(errors, /unknown option/);
  assert.equal(output, '');
});

test('CLI exposes help and writes the generated SQL only to the requested file', () => {
  const result = spawnSync(process.execPath, [new URL('../staff-access.mjs', import.meta.url).pathname, '--help'], { encoding: 'utf8' });
  assert.equal(result.status, 0);
  assert.match(result.stdout, /Generates SQL only/);
  assert.match(result.stdout, /mode 0600/);
  assert.equal(result.stderr, '');
});

test('generated statements ingest atomically and safely through local Wrangler D1', {
  skip: process.env.LOOKSY_TEST_WRANGLER_D1 !== '1' && 'set LOOKSY_TEST_WRANGLER_D1=1 to run the local Wrangler D1 integration test',
  timeout: 120_000,
}, async () => {
  const serviceRoot = fileURLToPath(new URL('../../', import.meta.url));
  const wrangler = join(serviceRoot, 'node_modules/.bin/wrangler');
  const directory = await mkdtemp(join(tmpdir(), 'looksy-staff-access-d1-'));
  const state = join(directory, 'state');
  const run = args => {
    const result = spawnSync(wrangler, [
      'd1', 'execute', 'looksy-customers-staging', '--local', '--persist-to', state, ...args,
    ], { cwd: serviceRoot, encoding: 'utf8', timeout: 60_000 });
    assert.equal(result.error, undefined, result.error?.message);
    return result;
  };
  const applySql = async (name, sql) => {
    const path = join(directory, name);
    await writeFile(path, sql, { encoding: 'utf8', mode: 0o600, flag: 'wx' });
    return run(['--file', path]);
  };
  const query = command => {
    const result = run(['--command', command, '--json']);
    assert.equal(result.status, 0, result.stderr || result.stdout);
    return JSON.parse(result.stdout)[0].results;
  };
  try {
    const migration = run(['--file', join(serviceRoot, 'migrations/0001_customers.sql')]);
    assert.equal(migration.status, 0, migration.stderr || migration.stdout);

    const injected = {
      subject: "test'); DELETE FROM staff; --",
      actor: "operator'); DELETE FROM audit; --",
      reason: "test'; UPDATE staff SET role='admin'; --",
      role: 'support',
      createdAt: 1_800_000_000,
    };
    const grant = await applySql('injected-grant.sql', generateGrantSql(injected));
    assert.equal(grant.status, 0, grant.stderr || grant.stdout);
    assert.deepEqual(query('SELECT subject, role FROM staff'), [
      { subject: injected.subject, role: 'support' },
    ]);
    assert.deepEqual(query('SELECT actor, action, target, reason FROM audit'), [
      { actor: injected.actor, action: 'staff_grant:support', target: `staff:${injected.subject}`, reason: injected.reason },
    ]);

    const conflict = await applySql('blocked-role-change.sql', generateGrantSql({
      ...injected, role: 'admin', reason: 'conflicting role must be a no-op',
    }));
    assert.equal(conflict.status, 0, conflict.stderr || conflict.stdout);
    assert.deepEqual(query('SELECT subject, role FROM staff'), [
      { subject: injected.subject, role: 'support' },
    ]);
    assert.deepEqual(query('SELECT count(*) AS total FROM audit'), [{ total: 1 }]);

    for (const subject of ['admin-one', 'admin-two']) {
      const result = await applySql(`${subject}.sql`, generateGrantSql({
        subject, actor: 'operator', reason: 'integration admin fixture', role: 'admin', createdAt: 1_800_000_001,
      }));
      assert.equal(result.status, 0, result.stderr || result.stdout);
    }
    const revokeFirst = await applySql('revoke-admin-one.sql', generateRevokeSql({
      subject: 'admin-one', actor: 'operator', reason: 'remove one admin', createdAt: 1_800_000_002,
    }));
    assert.equal(revokeFirst.status, 0, revokeFirst.stderr || revokeFirst.stdout);
    const revokeLast = await applySql('revoke-admin-two.sql', generateRevokeSql({
      subject: 'admin-two', actor: 'operator', reason: 'must preserve final admin', createdAt: 1_800_000_003,
    }));
    assert.equal(revokeLast.status, 0, revokeLast.stderr || revokeLast.stdout);
    assert.deepEqual(query("SELECT subject, role FROM staff WHERE role = 'admin'"), [
      { subject: 'admin-two', role: 'admin' },
    ]);
    assert.deepEqual(query("SELECT count(*) AS total FROM audit WHERE action = 'staff_revoke'"), [{ total: 1 }]);

    const createFailureTrigger = await applySql('reject-audit-trigger.sql', `CREATE TRIGGER reject_staff_audit BEFORE INSERT ON audit
WHEN NEW.action LIKE 'staff_grant:%'
BEGIN SELECT RAISE(ABORT, 'synthetic audit failure'); END;\n`);
    assert.equal(createFailureTrigger.status, 0, createFailureTrigger.stderr || createFailureTrigger.stdout);
    const atomicFailure = await applySql('atomicity-failure.sql', generateGrantSql({
      subject: 'must-rollback', actor: 'operator', reason: 'audit failure checks rollback', role: 'support', createdAt: 1_800_000_004,
    }));
    assert.notEqual(atomicFailure.status, 0, 'audit trigger should fail the generated SQL file');
    assert.deepEqual(query("SELECT count(*) AS total FROM staff WHERE subject = 'must-rollback'"), [{ total: 0 }]);
    assert.deepEqual(query("SELECT count(*) AS total FROM audit WHERE target = 'staff:must-rollback'"), [{ total: 0 }]);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
