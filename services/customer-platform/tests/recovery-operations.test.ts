import { env } from 'cloudflare:workers';
import { applyD1Migrations } from 'cloudflare:test';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { exportJWK, exportPKCS8, generateKeyPair, jwtVerify, SignJWT } from 'jose';
import worker from '../src/index';
import { deleteCustomer, operationsSummary, retryWebhook } from '../src/operations';
import { hash, hmacKey, now, type ServiceEnv } from '../src/core';

let service: ServiceEnv, baseService: ServiceEnv, publicKey: CryptoKey;
const credential = (symbol: string) => symbol.repeat(43);
async function call(path: string, input?: unknown, token?: string) {
  return worker.fetch(new Request(`https://accounts.looksy.test${path}`, {
    method: input === undefined ? 'GET' : 'POST',
    headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    body: input === undefined ? undefined : JSON.stringify(input),
  }), service);
}
function billing() {
  vi.stubGlobal('fetch', vi.fn(async (url: string) => {
    const path = new URL(url).pathname;
    const attributes: Record<string, Record<string, unknown>> = {
      '/v1/license-keys/10': { store_id: 1, customer_id: 7, order_id: 20, order_item_id: 30, product_id: 4,
        user_email: 'owner@example.test', status: 'active', expires_at: null, disabled: false },
      '/v1/orders/20': { store_id: 1, customer_id: 7, test_mode: true, status: 'paid', refunded: false },
      '/v1/order-items/30': { order_id: 20, product_id: 4, variant_id: 5 },
    };
    if (path === '/v1/licenses/validate') return Response.json({ valid: true, license_key: { id: 10 }, meta: { store_id: 1, variant_id: 5, product_id: 4 } });
    if (attributes[path]) { const parts = path.split('/'); return Response.json({ data: { type: parts[2], id: parts[3], attributes: attributes[path] } }); }
    throw new Error(`Unexpected provider request ${path}`);
  }));
}

beforeAll(async () => {
  await applyD1Migrations(env.DB, env.TEST_MIGRATIONS);
  const pair = await generateKeyPair('EdDSA', { extractable: true });
  publicKey = pair.publicKey;
  service = { ...env, ISSUER: 'https://accounts.looksy.test', LEMON_STORE_ID: '1',
    ENTITLEMENT_PRIVATE_KEY: await exportPKCS8(pair.privateKey), LEMON_API_KEY: 'test-only',
    LEMON_WEBHOOK_SECRET: 'webhook-test-secret-'.repeat(3), RATE_LIMIT_SECRET: 'rate-test-secret-'.repeat(3),
    PRODUCT_POLICY: JSON.stringify({ '5': { productId: '4', plan: 'studio', mode: 'perpetual', major: 1,
      features: ['editor'], deviceLimit: 1 } }) };
  baseService = { ...service };
});

beforeEach(async () => {
  service = { ...baseService };
  for (const table of ['audit', 'staff', 'rate_buckets', 'webhook_receipts', 'installations', 'licenses',
    'subscriptions', 'privacy_tombstones', 'customers', 'refresh_sequences']) await env.DB.prepare(`DELETE FROM ${table}`).run();
});
afterEach(() => vi.unstubAllGlobals());

describe('paid device recovery', () => {
  it('rotates an active installation atomically at the seat limit and is retry-idempotent', async () => {
    billing();
    expect((await call('/v1/activate', { licenseKey: 'provider-key' }, credential('a'))).status).toBe(200);
    const recovered = await call('/v1/recover', { licenseKey: 'provider-key', previousCredential: credential('a'), credential: credential('b') });
    expect(recovered.status).toBe(200);
    const body = await recovered.json() as { lease: string; installationId: string };
    const claims = (await jwtVerify(body.lease, publicKey)).payload;
    expect(claims.mode).toBe('perpetual');
    expect(claims.device).toBe(await hash(credential('b')));
    expect((await call('/v1/refresh', { version: '1.0.0', platform: 'mac-arm' }, credential('a'))).status).toBe(401);
    const retry = await call('/v1/recover', { licenseKey: 'provider-key', previousCredential: credential('a'), credential: credential('b') });
    expect(retry.status).toBe(200);
    expect((await retry.json() as { installationId: string }).installationId).toBe(body.installationId);
    expect(await env.DB.prepare('SELECT revoked FROM installations WHERE credential_hash=?').bind(await hash(credential('a'))).first())
      .toEqual({ revoked: 1 });
  });

  it('accepts a revoked paid installation as recovery proof and still enforces the device cap', async () => {
    billing();
    expect((await call('/v1/activate', { licenseKey: 'provider-key' }, credential('c'))).status).toBe(200);
    expect((await call('/v1/devices/revoke', { id: (await env.DB.prepare('SELECT id FROM installations').first<{id: string}>())!.id }, credential('c'))).status).toBe(200);
    const result = await call('/v1/recover', { licenseKey: 'provider-key', previousCredential: credential('c'), credential: credential('d') });
    expect(result.status).toBe(200);
    expect((await env.DB.prepare('SELECT count(*) AS count FROM installations WHERE license_id="10" AND revoked=0').first<{count: number}>())!.count).toBe(1);
    const full = await call('/v1/recover', { licenseKey: 'provider-key', credential: credential('e') });
    expect(full.status).toBe(409);
  });

  it('converts a guest installation to a verified paid installation without resetting its trial', async () => {
    const guest = await call('/v1/trial', { credential: credential('g') });
    expect(guest.status).toBe(200);
    const trial = await guest.json() as { lease: string };
    const oldClaims = (await jwtVerify(trial.lease, publicKey)).payload;
    billing();
    const recovered = await call('/v1/recover', { licenseKey: 'provider-key', previousCredential: credential('g'), credential: credential('h') });
    expect(recovered.status).toBe(200);
    expect((await jwtVerify((await recovered.json() as { lease: string }).lease, publicKey)).payload.mode).toBe('perpetual');
    expect(await env.DB.prepare('SELECT revoked,trial_ends_at FROM installations WHERE credential_hash=?').bind(await hash(credential('g'))).first())
      .toEqual({ revoked: 1, trial_ends_at: oldClaims.exp });
    expect((await call('/v1/trial', { credential: credential('g') })).status).toBe(403);
  });

  it('retains an active old installation when the atomic replacement cannot acquire a seat', async () => {
    billing();
    await call('/v1/activate', { licenseKey: 'provider-key' }, credential('j'));
    // Inject another already-active installation at the cap to force trigger rejection.
    await env.DB.prepare('UPDATE licenses SET device_limit=2').run();
    const first = await env.DB.prepare('SELECT * FROM installations WHERE license_id=?').bind('10').first<Record<string, unknown>>();
    await env.DB.prepare(`INSERT INTO installations(id,credential_hash,customer_id,license_id,trial_ends_at,created_at,last_seen_at)
      VALUES(?,?,?,?,?,?,?)`).bind('other-device', await hash(credential('k')), first!.customer_id, '10', 1, 2, 2).run();
    await env.DB.prepare('UPDATE licenses SET device_limit=1').run();
    const failed = await call('/v1/recover', { licenseKey: 'provider-key', previousCredential: credential('j'), credential: credential('l') });
    expect(failed.status).toBe(409);
    expect(await env.DB.prepare('SELECT revoked FROM installations WHERE credential_hash=?').bind(await hash(credential('j'))).first())
      .toEqual({ revoked: 0 });
  });
});

describe('customer operations', () => {
  it('anonymizes contact data and blocks provider sync from restoring it', async () => {
    billing();
    await call('/v1/activate', { licenseKey: 'provider-key' }, credential('m'));
    const result = await deleteCustomer(service, { subject: 'admin-1', role: 'admin' }, 'lemon:1:7', 'Customer data request');
    expect(result.status).toBe(200);
    const { syncLicense } = await import('../src/billing');
    await syncLicense(service, '10');
    expect(await env.DB.prepare('SELECT email,provider_customer_id FROM customers WHERE id=?').bind('lemon:1:7').first())
      .toEqual({ email: null, provider_customer_id: null });
    expect(await env.DB.prepare('SELECT blocked,status FROM licenses WHERE id=?').bind('10').first())
      .toEqual({ blocked: 1, status: 'deleted' });
    expect(await env.DB.prepare('SELECT revoked FROM installations WHERE license_id=?').bind('10').first())
      .toEqual({ revoked: 1 });
  });

  it('reports retry backlog and only permits audited staff retry', async () => {
    const digest = 'a'.repeat(64);
    await env.DB.prepare(`INSERT INTO webhook_receipts(digest,event,resource_id,received_at,attempts,next_retry_at,last_error_code)
      VALUES(?,?,?,?,?,?,?)`).bind(digest, 'order_created', '20', 123, 3, 100, 'processing_failed').run();
    const summary = await operationsSummary(service);
    const health = await summary.json() as { webhooks: { pending: number; failed: number; due: number; maxAttempts: number } };
    expect(health.webhooks).toEqual({ pending: 1, failed: 1, due: 1, oldestPendingAt: 123, maxAttempts: 3 });
    await expect(retryWebhook(service, { subject: 'support-1', role: 'support' }, digest, 'Retry billing event'))
      .rejects.toMatchObject({ status: 403 });
    expect((await retryWebhook(service, { subject: 'admin-1', role: 'admin' }, digest, 'Retry billing event')).status).toBe(200);
    expect(await env.DB.prepare('SELECT attempts,next_retry_at,last_error_code FROM webhook_receipts WHERE digest=?').bind(digest).first())
      .toEqual({ attempts: 0, next_retry_at: expect.any(Number), last_error_code: null });
    expect(await env.DB.prepare('SELECT action,target FROM audit').first()).toEqual({ action: 'webhook_retry_requested', target: `webhook:${digest}` });
  });

  it('accepts a signed Access service token for support health reads only', async () => {
    const accessKeys = await generateKeyPair('RS256', { extractable: true });
    const jwk = { ...await exportJWK(accessKeys.publicKey), kid: 'operations-service', alg: 'RS256', use: 'sig' };
    const issuer = 'https://looksy.cloudflareaccess.com';
    service = { ...service, ACCESS_TEAM_DOMAIN: 'looksy.cloudflareaccess.com', ACCESS_AUDIENCE: 'staff-app' };
    await env.DB.prepare('INSERT INTO staff(subject,role) VALUES(?,?)').bind('service:client-id-123456.access', 'support').run();
    vi.stubGlobal('fetch', vi.fn(async (url: string) => {
      if (url === `${issuer}/cdn-cgi/access/certs`) return Response.json({ keys: [jwk] });
      throw new Error('unexpected Access identity request');
    }));
    const token = await new SignJWT({ type: 'app', common_name: 'client-id-123456.access' })
      .setProtectedHeader({ alg: 'RS256', kid: 'operations-service' }).setIssuer(issuer).setAudience('staff-app')
      .setSubject('').setIssuedAt().setExpirationTime('5m').sign(accessKeys.privateKey);
    const request = (path: string, method = 'GET') => worker.fetch(new Request(`https://accounts.looksy.test${path}`, {
      method, headers: { 'Cf-Access-Jwt-Assertion': token,
        ...(method === 'POST' ? { Origin: 'https://accounts.looksy.test', 'X-Looksy-Admin': '1', 'Content-Type': 'application/json' } : {}) },
      body: method === 'POST' ? JSON.stringify({ digest: 'a'.repeat(64), reason: 'Service retry attempt' }) : undefined,
    }), service);
    expect((await request('/admin/api/operations')).status).toBe(200);
    expect((await request('/admin/api/customers')).status).toBe(403);
    expect((await request('/admin/api/webhooks')).status).toBe(403);
    expect((await request('/admin/api/webhook-retry', 'POST')).status).toBe(403);
  });

  it('persists failed deliveries with bounded exponential retry and stops automatic retries at eight', async () => {
    const raw = JSON.stringify({ meta: { event_name: 'license_key_updated', test_mode: true }, data: { id: '10' } });
    const signature = [...new Uint8Array(await crypto.subtle.sign('HMAC', await hmacKey(service.LEMON_WEBHOOK_SECRET),
      new TextEncoder().encode(raw)))].map(value => value.toString(16).padStart(2, '0')).join('');
    vi.stubGlobal('fetch', vi.fn(async () => { throw new Error('synthetic provider outage'); }));
    const send = () => worker.fetch(new Request('https://accounts.looksy.test/v1/webhooks/lemon', {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Signature': signature }, body: raw,
    }), service);
    expect((await send()).status).toBe(502);
    let row = await env.DB.prepare('SELECT attempts,next_retry_at FROM webhook_receipts').first<{attempts: number; next_retry_at: number}>();
    expect(row!.attempts).toBe(1);
    expect(row!.next_retry_at).toBeGreaterThanOrEqual(now() + 60);
    expect((await send()).status).toBe(202);
    expect((await env.DB.prepare('SELECT attempts FROM webhook_receipts').first()).attempts).toBe(1);
    for (let attempt = 1; attempt < 8; attempt++) {
      await env.DB.prepare('UPDATE webhook_receipts SET next_retry_at=0').run();
      expect((await send()).status).toBe(502);
    }
    row = await env.DB.prepare('SELECT attempts,next_retry_at FROM webhook_receipts').first<{attempts: number; next_retry_at: number}>();
    expect(row!.attempts).toBe(8);
    expect((await send()).status).toBe(202);
    expect((await env.DB.prepare('SELECT attempts FROM webhook_receipts').first()).attempts).toBe(8);
  });
});
