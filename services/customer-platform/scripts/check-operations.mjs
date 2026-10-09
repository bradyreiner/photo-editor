#!/usr/bin/env node
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

// Consume a staff-authorized operations response without storing Access tokens.
export function evaluateOperations(snapshot, { now = Math.floor(Date.now() / 1000), maxPendingAge = 7200, maxSnapshotAge = 900 } = {}) {
  const integer = value => Number.isSafeInteger(value) && value >= 0;
  const valid = snapshot && integer(snapshot.generatedAt) && snapshot.webhooks && snapshot.licenses &&
    ['pending', 'failed', 'due', 'maxAttempts'].every(key => integer(snapshot.webhooks[key])) &&
    integer(snapshot.licenses.stale) &&
    (snapshot.webhooks.oldestPendingAt === null || integer(snapshot.webhooks.oldestPendingAt));
  if (!valid) throw new TypeError('Invalid operations response');
  const alerts = [];
  if (snapshot.generatedAt > now + 60 || now - snapshot.generatedAt > maxSnapshotAge) alerts.push('operations_snapshot_stale');
  if (snapshot.webhooks.oldestPendingAt !== null && now - snapshot.webhooks.oldestPendingAt > maxPendingAge) alerts.push('webhook_backlog_old');
  if (snapshot.webhooks.maxAttempts >= 8) alerts.push('webhook_retry_exhausted');
  if (snapshot.webhooks.failed > 0) alerts.push('webhook_processing_failures');
  if (snapshot.licenses.stale > 0) alerts.push('license_reconciliation_stale');
  return { healthy: alerts.length === 0, alerts, generatedAt: snapshot.generatedAt };
}

export async function runCli(args, { stdout = process.stdout, stderr = process.stderr } = {}) {
  if (args.length === 1 && args[0] === '--help') {
    stdout.write('Usage: node scripts/check-operations.mjs <private-operations-response.json>\nFetch GET /admin/api/operations with your staff Access session first. Exit 0 = healthy, 1 = alerts, 2 = invalid input. No customer fields or credentials are printed.\n');
    return 0;
  }
  try {
    if (args.length !== 1) throw new TypeError('Expected one operations JSON file');
    const report = evaluateOperations(JSON.parse(await readFile(resolve(args[0]), 'utf8')));
    stdout.write(JSON.stringify(report) + '\n');
    return report.healthy ? 0 : 1;
  } catch {
    stderr.write('Cannot validate operations response; check the file and response schema.\n');
    return 2;
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) process.exitCode = await runCli(process.argv.slice(2));
