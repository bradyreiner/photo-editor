#!/usr/bin/env node
import { createHash, randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { appendFile, chmod, lstat, mkdir, readFile, realpath, writeFile } from 'node:fs/promises';
import { dirname, resolve, relative, isAbsolute } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const execute = promisify(execFile);
const serviceRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repositoryRoot = resolve(serviceRoot, '../..');
const wrangler = resolve(serviceRoot, 'node_modules/wrangler/bin/wrangler.js');
const requiredTables = ['customers', 'licenses', 'installations', 'webhook_receipts', 'staff', 'audit'];

async function command(program, args, cwd) {
  try {
    return await execute(program, args, { cwd, env: { ...process.env, WRANGLER_LOG_PATH: resolve(cwd, 'wrangler.log'), CI: 'true' }, maxBuffer: 64 * 1024 * 1024, timeout: 180000 });
  } catch (error) {
    // SQL, provider errors and command output can contain customer data.
    await appendFile(resolve(cwd, 'command-errors.log'), `${error.stdout ?? ''}\n${error.stderr ?? ''}\n`, { mode: 0o600 }).catch(() => {});
    throw new Error('Database command failed; inspect private Wrangler logs in the recovery directory.');
  }
}

async function outsideRepository(path) {
  const relation = relative(await realpath(repositoryRoot), path);
  if (relation === '' || (!relation.startsWith('..' + '/') && !isAbsolute(relation))) throw new Error('Recovery artifacts must be outside the repository.');
}

async function privateDirectory(path) {
  const target = resolve(path);
  const parent = await realpath(dirname(target));
  const actual = resolve(parent, target.split('/').at(-1));
  await outsideRepository(actual);
  await mkdir(actual, { mode: 0o700 }); // Exclusively create; never overwrite a backup or existing state.
  await chmod(actual, 0o700);
  return actual;
}

async function hashFile(path) {
  const stat = await lstat(path);
  if (!stat.isFile() || stat.isSymbolicLink()) throw new Error('Backup SQL must be a regular file.');
  return createHash('sha256').update(await readFile(path)).digest('hex');
}

// Inspect an exported snapshot using SQLite, rather than racing live database counts.
async function inspectSql(path, cwd) {
  const script = `import sqlite3,sys,json,hashlib\nc=sqlite3.connect(':memory:')\nc.executescript(open(sys.argv[1],encoding='utf8').read())\nassert c.execute('PRAGMA integrity_check').fetchone()[0]=='ok'\nassert not c.execute('PRAGMA foreign_key_check').fetchall()\ntables=[r[0] for r in c.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' AND name NOT LIKE '_cf_%' ORDER BY name")]\ncounts={}\nfingerprints={}\nfor name in tables:\n cursor=c.execute('SELECT * FROM "'+name.replace('"','""')+'"')\n rows=sorted(json.dumps(row,ensure_ascii=True,default=lambda x:{'blob':x.hex()}) for row in cursor)\n counts[name]=len(rows)\n columns=json.dumps([col[0] for col in cursor.description])\n fingerprints[name]=hashlib.sha256((columns+'\\n'+'\\n'.join(rows)).encode()).hexdigest()\nprint(json.dumps({'counts':counts,'fingerprints':fingerprints},sort_keys=True))\n`;
  const result = await command('python3', ['-c', script, path], cwd);
  const snapshot = JSON.parse(result.stdout);
  if (!requiredTables.every(name => Number.isSafeInteger(snapshot.counts[name]))) throw new Error('Backup is missing required customer-platform tables.');
  return snapshot;
}

export async function backupDatabase({ config, directory, mode }) {
  if (!['local', 'remote'].includes(mode)) throw new Error('Select --local or --remote explicitly.');
  const configPath = await realpath(resolve(config));
  const output = await privateDirectory(directory);
  const sql = resolve(output, 'database.sql');
  // Pre-create with restrictive permissions; Wrangler exports into this exclusive directory.
  await writeFile(sql, '', { flag: 'wx', mode: 0o600 });
  await command(process.execPath, [wrangler, 'd1', 'export', 'DB', '--config', configPath, `--${mode}`, '--output', sql], output);
  await chmod(sql, 0o600);
  const manifest = { schema: 1, createdAt: new Date().toISOString(), mode, sha256: await hashFile(sql), snapshot: await inspectSql(sql, output) };
  await writeFile(resolve(output, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n', { flag: 'wx', mode: 0o600 });
  return { directory: output, sha256: manifest.sha256, tableCount: Object.keys(manifest.snapshot.counts).length };
}

export async function rehearseRestore({ backup, directory }) {
  const input = await realpath(resolve(backup));
  await outsideRepository(input);
  const manifest = JSON.parse(await readFile(resolve(input, 'manifest.json'), 'utf8'));
  const sql = resolve(input, 'database.sql');
  if (manifest.schema !== 1 || !/^[a-f0-9]{64}$/.test(manifest.sha256) || await hashFile(sql) !== manifest.sha256) throw new Error('Backup checksum failed. No restore was attempted.');
  const expected = await inspectSql(sql, input);
  if (JSON.stringify(expected) !== JSON.stringify(manifest.snapshot)) throw new Error('Backup table contents do not match the manifest.');
  const output = await privateDirectory(directory);
  const config = resolve(output, 'wrangler.json');
  await writeFile(config, JSON.stringify({ name: 'looksy-isolated-restore', compatibility_date: '2026-09-23', d1_databases: [{ binding: 'DB', database_name: 'looksy-isolated-restore', database_id: randomUUID() }] }), { flag: 'wx', mode: 0o600 });
  // A fresh, private config owns fresh local state. There is deliberately no remote restore path.
  await command(process.execPath, [wrangler, 'd1', 'execute', 'DB', '--config', config, '--local', '--file', sql], output);
  const restored = resolve(output, 'restored.sql');
  await writeFile(restored, '', { flag: 'wx', mode: 0o600 });
  await command(process.execPath, [wrangler, 'd1', 'export', 'DB', '--config', config, '--local', '--output', restored], output);
  await chmod(restored, 0o600);
  const actual = await inspectSql(restored, output);
  if (JSON.stringify(actual) !== JSON.stringify(expected)) throw new Error('Restored table contents differ from the snapshot.');
  const report = { passed: true, checkedAt: new Date().toISOString(), sourceSha256: manifest.sha256, snapshot: actual, scope: 'isolated-local-restore' };
  await writeFile(resolve(output, 'restore-report.json'), JSON.stringify(report, null, 2) + '\n', { flag: 'wx', mode: 0o600 });
  return { directory: output, passed: true, tableCount: Object.keys(actual.counts).length };
}

export async function runCli(args, { stdout = process.stdout, stderr = process.stderr } = {}) {
  if (args.length === 1 && args[0] === '--help') {
    stdout.write('Usage:\n  node scripts/database-recovery.mjs backup --config <wrangler.json> --directory <new-private-directory> --local|--remote\n  node scripts/database-recovery.mjs rehearse --backup <backup-directory> --directory <new-private-directory>\nBackup exports D1 read-only. Rehearse imports only into new isolated local state. Artifacts must be outside the checkout; directories and files are private. Never commits, deploys, or restores production. Requires Wrangler and python3.\n');
    return 0;
  }
  try {
    const [operation, ...rest] = args;
    if (!['backup', 'rehearse'].includes(operation)) throw new Error('Expected backup or rehearse.');
    const options = {};
    for (let i = 0; i < rest.length; i++) {
      const flag = rest[i];
      if (['--local', '--remote'].includes(flag) && operation === 'backup') {
        if (options.mode) throw new Error('Select only one database mode.');
        options.mode = flag.slice(2);
      } else if (['--directory', ...(operation === 'backup' ? ['--config'] : ['--backup'])].includes(flag)) {
        const key = flag.slice(2);
        if (options[key] || !rest[i + 1] || rest[i + 1].startsWith('--')) throw new Error('Missing or repeated option.');
        options[key] = rest[++i];
      } else throw new Error('Unknown option.');
    }
    if (!options.directory || (operation === 'backup' ? !options.config || !options.mode : !options.backup)) throw new Error('Missing required options; see --help.');
    const result = await (operation === 'backup' ? backupDatabase(options) : rehearseRestore(options));
    stdout.write(JSON.stringify(result) + '\n');
    return 0;
  } catch (error) {
    stderr.write(`${error instanceof Error ? error.message : 'Recovery operation failed.'}\n`);
    return 2;
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) process.exitCode = await runCli(process.argv.slice(2));
