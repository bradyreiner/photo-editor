import { test } from 'node:test';
import assert from 'node:assert/strict';
import { monitorOperations, runCli } from '../monitor-operations.mjs';
const env = { LOOKSY_OPERATIONS_ORIGIN: 'https://customers.looksy.test', LOOKSY_ACCESS_CLIENT_ID: 'synthetic-id', LOOKSY_ACCESS_CLIENT_SECRET: 'synthetic-secret' };
const snapshot = { generatedAt: 10000, webhooks: { pending: 0, failed: 0, due: 0, maxAttempts: 0, oldestPendingAt: null }, licenses: { stale: 0 } };
test('monitor requests staff health without redirects and returns only bounded operational signals', async () => {
  const result = await monitorOperations({ env, now: 10000, fetchImpl: async (url, options) => {
    assert.equal(url.href, 'https://customers.looksy.test/admin/api/operations');
    assert.equal(options.redirect, 'manual');
    assert.equal(options.headers['CF-Access-Client-Secret'], 'synthetic-secret');
    return Response.json({ ...snapshot, extraCustomerData: 'not-for-output' });
  } });
  assert.equal(result.healthy, true);
  assert.doesNotMatch(JSON.stringify(result), /synthetic-secret|not-for-output/);
});
test('monitor refuses insecure origins, auth redirects, non-JSON and oversized responses', async () => {
  await assert.rejects(monitorOperations({ env: { ...env, LOOKSY_OPERATIONS_ORIGIN: 'http://customers.looksy.test' }, fetchImpl() { throw new Error('must not fetch'); } }), /origin/);
  await assert.rejects(monitorOperations({ env, fetchImpl: async () => new Response('', { status: 302 }) }), /unauthorized/);
  await assert.rejects(monitorOperations({ env, fetchImpl: async () => new Response('login page') }), /unauthorized/);
  await assert.rejects(monitorOperations({ env, fetchImpl: async () => new Response('x'.repeat(32769), { headers: { 'content-type': 'application/json' } }) }), /Oversized/);
});
test('CLI error never exposes thrown customer data or Access token', async () => {
  let output = '';
  const result = await runCli({ env, fetchImpl: async () => { throw new Error('synthetic-secret private@example.invalid'); }, stderr: { write(value) { output += value; } } });
  assert.equal(result, 2);
  assert.doesNotMatch(output, /synthetic-secret|private@example/);
});
