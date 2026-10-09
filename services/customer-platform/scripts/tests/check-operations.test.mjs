import { test } from 'node:test';
import assert from 'node:assert/strict';
import { evaluateOperations, runCli } from '../check-operations.mjs';

const healthy = () => ({ generatedAt: 10000, webhooks: { pending: 0, failed: 0, due: 0, maxAttempts: 0, oldestPendingAt: null }, licenses: { stale: 0 } });
test('operations checker distinguishes fresh healthy responses from stale and malformed responses', () => {
  assert.deepEqual(evaluateOperations(healthy(), { now: 10000 }).alerts, []);
  assert.deepEqual(evaluateOperations(healthy(), { now: 12000 }).alerts, ['operations_snapshot_stale']);
  assert.deepEqual(evaluateOperations(healthy(), { now: 9900 }).alerts, ['operations_snapshot_stale']);
  assert.throws(() => evaluateOperations({}));
  const negative = healthy(); negative.webhooks.pending = -1;
  assert.throws(() => evaluateOperations(negative));
});
test('backlog, exhausted retries, failed receipts and stale licenses produce independent signals', () => {
  const input = healthy();
  input.webhooks = { pending: 2, failed: 2, due: 2, maxAttempts: 8, oldestPendingAt: 1 };
  input.licenses.stale = 3;
  assert.deepEqual(evaluateOperations(input, { now: 10000 }).alerts, ['webhook_backlog_old', 'webhook_retry_exhausted', 'webhook_processing_failures', 'license_reconciliation_stale']);
  input.webhooks.maxAttempts = 7; input.webhooks.oldestPendingAt = 9000; input.licenses.stale = 0;
  assert.deepEqual(evaluateOperations(input, { now: 10000 }).alerts, ['webhook_processing_failures']);
});
test('invalid file CLI errors never echo file contents or raw filesystem messages', async () => {
  let message = '';
  assert.equal(await runCli(['/missing/private-customer-email.json'], { stdout: { write() {} }, stderr: { write(text) { message += text; } } }), 2);
  assert.doesNotMatch(message, /customer-email/);
});
