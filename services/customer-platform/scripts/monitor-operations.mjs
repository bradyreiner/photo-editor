#!/usr/bin/env node
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { evaluateOperations } from './check-operations.mjs';

export async function monitorOperations({ env = process.env, fetchImpl = fetch, now } = {}) {
  const url = new URL(env.LOOKSY_OPERATIONS_ORIGIN || 'invalid');
  if (url.protocol !== 'https:' || url.username || url.password || url.pathname !== '/' || url.search || url.hash || url.hostname.endsWith('.invalid')) throw new Error('Invalid operations origin.');
  if (!env.LOOKSY_ACCESS_CLIENT_ID || !env.LOOKSY_ACCESS_CLIENT_SECRET) throw new Error('Configure the staff Access service-token credentials.');
  const response = await fetchImpl(new URL('/admin/api/operations', url), {
    headers: { 'CF-Access-Client-Id': env.LOOKSY_ACCESS_CLIENT_ID, 'CF-Access-Client-Secret': env.LOOKSY_ACCESS_CLIENT_SECRET, Accept: 'application/json' },
    redirect: 'manual', signal: AbortSignal.timeout(15000),
  });
  if (response.status !== 200 || !response.headers.get('content-type')?.startsWith('application/json')) throw new Error('Operations endpoint unavailable or unauthorized.');
  const reader = response.body?.getReader();
  if (!reader) throw new Error('Missing operations response.');
  const pieces = []; let total = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      total += value.byteLength;
      if (total > 32768) throw new Error('Oversized operations response.');
      pieces.push(Buffer.from(value));
    }
  } finally { await reader.cancel().catch(() => {}); }
  return evaluateOperations(JSON.parse(Buffer.concat(pieces).toString('utf8')), now === undefined ? {} : { now });
}

export async function runCli({ stdout = process.stdout, stderr = process.stderr, ...options } = {}) {
  try {
    const report = await monitorOperations(options);
    stdout.write(JSON.stringify(report) + '\n');
    return report.healthy ? 0 : 1;
  } catch {
    stderr.write('Customer operations check failed. Verify endpoint, staff role, Access service token and service availability.\n');
    return 2;
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) process.exitCode = await runCli();
