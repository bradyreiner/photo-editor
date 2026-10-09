import { env } from 'cloudflare:workers';
import { applyD1Migrations } from 'cloudflare:test';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { exportJWK, exportPKCS8, generateKeyPair, jwtVerify, SignJWT } from 'jose';
import worker from '../src/index';
import { hash, type ServiceEnv } from '../src/core';

let service: ServiceEnv;
let baseService: ServiceEnv;
let publicKey: CryptoKey;
const credential = (value = 'a') => value.repeat(43);

async function call(path: string, input?: unknown, token?: string, extra: Record<string, string> = {}) {
  return worker.fetch(new Request(`https://accounts.looksy.test${path}`, {
    method: input === undefined ? 'GET' : 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
      ...extra,
    },
    body: input === undefined ? undefined : JSON.stringify(input),
  }), service);
}

function billing(variant = '5') {
  vi.stubGlobal('fetch', vi.fn(async (url: string) => {
    const path = new URL(url).pathname;
    const attributes: Record<string, Record<string, unknown>> = {
      '/v1/license-keys/10': {
        store_id: 1, customer_id: 7, order_id: 20, order_item_id: 30,
        product_id: 4, user_email: 'owner@example.test', status: 'active', expires_at: null, disabled: false,
      },
      '/v1/orders/20': { store_id: 1, customer_id: 7, test_mode: true, status: 'paid', refunded: false },
      '/v1/order-items/30': { order_id: 20, product_id: 4, variant_id: Number(variant) },
    };
    if (path === '/v1/licenses/validate') {
      return Response.json({ valid: true, license_key: { id: 10 }, meta: { store_id: 1, variant_id: variant, product_id: 4 } });
    }
    if (attributes[path]) {
      const parts = path.split('/');
      return Response.json({ data: { type: parts[2], id: parts[3], attributes: attributes[path] } });
    }
    throw new Error('Unexpected provider request');
  }));
}

beforeAll(async () => {
  await applyD1Migrations(env.DB, env.TEST_MIGRATIONS);
  const pair = await generateKeyPair('EdDSA', { extractable: true });
  publicKey = pair.publicKey;
  baseService = {
    ...env,
    ISSUER: 'https://accounts.looksy.test',
    LEMON_STORE_ID: '1',
    ENTITLEMENT_PRIVATE_KEY: await exportPKCS8(pair.privateKey),
    LEMON_API_KEY: 'test-only',
    LEMON_WEBHOOK_SECRET: 'webhook-test-secret-'.repeat(3),
    RATE_LIMIT_SECRET: 'rate-test-secret-'.repeat(3),
    PRODUCT_POLICY: JSON.stringify({
      '5': { productId: '4', plan: 'studio', mode: 'perpetual', major: 1, features: ['editor', 'agent'], deviceLimit: 2 },
    }),
  };
});

beforeEach(async () => {
  service = { ...baseService };
  for (const table of ['audit', 'staff', 'rate_buckets', 'webhook_receipts', 'installations', 'licenses', 'subscriptions', 'privacy_tombstones', 'customers', 'refresh_sequences']) {
    await env.DB.prepare(`DELETE FROM ${table}`).run();
  }
});

afterEach(() => vi.unstubAllGlobals());

describe('customer and staff capability boundaries', () => {
  it('keeps guest entitlements server-defined when request fields claim elevated access', async () => {
    const response = await call('/v1/trial', {
      credential: credential(),
      role: 'admin',
      isAdmin: true,
      features: ['editor', 'agent', 'support', 'admin', 'beta'],
    });
    expect(response.status).toBe(200);
    const { lease } = await response.json() as { lease: string };
    const { payload } = await jwtVerify(lease, publicKey, {
      issuer: service.ISSUER,
      audience: 'looksy-desktop',
      algorithms: ['EdDSA'],
    });
    expect(payload.mode).toBe('trial');
    expect(payload.features).toEqual(['editor', 'agent']);
    expect(payload.role).toBeUndefined();
    expect(payload.isAdmin).toBeUndefined();
  });

  it('signs only configured editing capabilities for an editor-only purchase', async () => {
    service = {
      ...service,
      PRODUCT_POLICY: JSON.stringify({
        '7': { productId: '4', plan: 'solo', mode: 'perpetual', major: 1, features: ['editor'], deviceLimit: 1 },
      }),
    };
    billing('7');
    const response = await call('/v1/activate', { licenseKey: 'synthetic-license-key' }, credential());
    expect(response.status).toBe(200);
    const { lease } = await response.json() as { lease: string };
    const { payload } = await jwtVerify(lease, publicKey, {
      issuer: service.ISSUER,
      audience: 'looksy-desktop',
      algorithms: ['EdDSA'],
    });
    expect(payload.features).toEqual(['editor']);
    expect(payload.role).toBeUndefined();
    expect(payload.support).toBeUndefined();
    expect(payload.admin).toBeUndefined();
  });

  it('does not let a revoked guest installation escape revocation by activating a purchase', async () => {
    const token = credential();
    const trial = await call('/v1/trial', { credential: token });
    expect(trial.status).toBe(200);
    const { installationId } = await trial.json() as { installationId: string };
    expect((await call('/v1/devices/revoke', { id: installationId }, token)).status).toBe(200);

    billing();
    const activation = await call('/v1/activate', { licenseKey: 'synthetic-license-key' }, token);
    expect(activation.status).toBe(403);
    const installation = await env.DB.prepare('SELECT customer_id,license_id,revoked FROM installations WHERE id=?')
      .bind(installationId).first<{ customer_id: string; license_id: string | null; revoked: number }>();
    expect(installation).toMatchObject({ customer_id: `guest:${await hash(token)}`, license_id: null, revoked: 1 });
  });

  it('denies staff API access without an Access JWT or with a JWT signed by an untrusted key', async () => {
    const accessService = {
      ...service,
      ACCESS_TEAM_DOMAIN: 'looksy.cloudflareaccess.com',
      ACCESS_AUDIENCE: 'staff-app',
    };
    const missing = await worker.fetch(new Request('https://accounts.looksy.test/admin/api/customers'), accessService);
    expect(missing.status).toBe(401);

    const trustedPair = await generateKeyPair('RS256', { extractable: true });
    const attackerPair = await generateKeyPair('RS256', { extractable: true });
    const trustedJwk = { ...await exportJWK(trustedPair.publicKey), kid: 'staff-key', alg: 'RS256', use: 'sig' };
    vi.stubGlobal('fetch', vi.fn(async () => Response.json({ keys: [trustedJwk] })));
    const forged = await new SignJWT({})
      .setProtectedHeader({ alg: 'RS256', kid: 'staff-key' })
      .setIssuer('https://looksy.cloudflareaccess.com')
      .setAudience('staff-app')
      .setSubject('staff-subject')
      .setIssuedAt()
      .setExpirationTime('5m')
      .sign(attackerPair.privateKey);
    const rejected = await worker.fetch(new Request('https://accounts.looksy.test/admin/api/customers', {
      headers: { 'Cf-Access-Jwt-Assertion': forged },
    }), accessService);
    expect(rejected.status).toBe(401);
  });
});
