import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import { checkSetup } from '../check-setup.mjs';

function fixture() {
  const pair = generateKeyPairSync('ed25519');
  return {
    config: { workers_dev: false, preview_urls: false, assets: { run_worker_first: true },
      d1_databases: [{ binding: 'DB', database_id: '12345678-1234-1234-1234-123456789abc' }],
      vars: { ISSUER: 'https://accounts.looksy.test', SIGNING_KEY_ID: 'looksy-staging-1',
        ACCESS_TEAM_DOMAIN: 'looksy.cloudflareaccess.com', ACCESS_AUDIENCE: 'access-app-audience',
        LEMON_STORE_ID: '1', TRIAL_DAYS: '14', LEASE_DAYS: '7', ALLOW_TEST_PURCHASES: 'true',
        PRODUCT_POLICY: JSON.stringify({ '5': { productId: '4', plan: 'studio', mode: 'perpetual', major: 0, features: ['editor', 'agent'], deviceLimit: 2 } }) } },
    publicKey: { kid: 'looksy-staging-1', publicKeyBase64: Buffer.from(pair.publicKey.export({ format: 'jwk' }).x, 'base64url').toString('base64') },
    secrets: { ENTITLEMENT_PRIVATE_KEY: pair.privateKey.export({ format: 'pem', type: 'pkcs8' }),
      LEMON_API_KEY: 'synthetic-provider-credential', LEMON_WEBHOOK_SECRET: 'w'.repeat(48), RATE_LIMIT_SECRET: 'r'.repeat(48) }
  };
}

test('accepts complete offline staging configuration and matching signing pair', () => {
  const f = fixture(); assert.deepEqual(checkSetup(f.config, f), []);
});
test('rejects a wrong signing pair and mismatched key ID', () => {
  const f = fixture(); f.publicKey = fixture().publicKey; f.publicKey.kid = 'other';
  const errors = checkSetup(f.config, f);
  assert(errors.some(error => error.includes('key ID'))); assert(errors.some(error => error.includes('must match the desktop')));
});
test('denies staff roles in customer features and test purchases in production', () => {
  const f = fixture(); const policy = JSON.parse(f.config.vars.PRODUCT_POLICY); policy['5'].features.push('admin');
  f.config.vars.PRODUCT_POLICY = JSON.stringify(policy);
  const errors = checkSetup(f.config, { ...f, environment: 'production' });
  assert(errors.some(error => error.includes('PRODUCT_POLICY'))); assert(errors.some(error => error.includes('ALLOW_TEST_PURCHASES')));
});
test('rejects issuer paths, placeholders, shared secrets and missing provider credentials without echoing values', () => {
  const f = fixture(); f.config.vars.ISSUER += '/path'; f.config.vars.ACCESS_AUDIENCE = '<replace me>';
  f.secrets.LEMON_API_KEY = ''; f.secrets.LEMON_WEBHOOK_SECRET = f.secrets.RATE_LIMIT_SECRET = 'sensitive-value-'.repeat(4);
  const errors = checkSetup(f.config, f); assert(errors.length >= 4);
  assert(!errors.join().includes('sensitive-value')); assert(!errors.join().includes('<replace me>'));
});
test('requires secret verification and rejects unconfigured checkout placeholders', () => {
  const errors = checkSetup({ vars: { ISSUER: 'https://accounts.example.invalid', PRODUCT_POLICY: '{}' } });
  assert(errors.some(error => error.includes('ISSUER'))); assert(errors.some(error => error.includes('Worker secrets')));
});
test('accepts explicitly selected production policy with test purchases disabled', () => {
  const f = fixture(); f.config.vars.ALLOW_TEST_PURCHASES = 'false';
  assert.deepEqual(checkSetup(f.config, { ...f, environment: 'production' }), []);
});
test('reports malformed database bindings and null policy numbers as invalid', () => {
  const f = fixture(); f.config.d1_databases = {};
  const policy = JSON.parse(f.config.vars.PRODUCT_POLICY); policy['5'].major = null;
  f.config.vars.PRODUCT_POLICY = JSON.stringify(policy);
  const errors = checkSetup(f.config, f);
  assert(errors.some(error => error.includes('DB requires'))); assert(errors.some(error => error.includes('PRODUCT_POLICY')));
});

test('rejects perpetual major mismatches and permits an explicit future release version', () => {
  const f = fixture();
  const policy = JSON.parse(f.config.vars.PRODUCT_POLICY);
  policy['5'].major = 1;
  f.config.vars.PRODUCT_POLICY = JSON.stringify(policy);
  assert(checkSetup(f.config, f).some(error => error.includes('perpetual PRODUCT_POLICY major')));
  assert.deepEqual(checkSetup(f.config, { ...f, appVersion: '1.0.0' }), []);
  assert(checkSetup(f.config, { ...f, appVersion: 'release' }).some(error => error.includes('appVersion')));
});

test('validates trusted overlap keys and rejects malformed or conflicting key IDs', () => {
  const f = fixture();
  const extra = generateKeyPairSync('ed25519').publicKey.export({ format: 'jwk' }).x;
  f.publicKey.trustedKeys = { 'looksy-staging-previous': Buffer.from(extra, 'base64url').toString('base64') };
  assert.deepEqual(checkSetup(f.config, f), []);
  f.publicKey.trustedKeys.bad = 'nope';
  assert(checkSetup(f.config, f).some(error => error.includes('trustedKeys contains')));
  f.publicKey.trustedKeys = { [f.publicKey.kid]: 'A'.repeat(44) };
  assert(checkSetup(f.config, f).some(error => error.includes('conflicts with the primary')));
});


test('rejects primary and trusted key IDs longer than the native 80-character limit', () => {
  const f = fixture();
  f.config.vars.SIGNING_KEY_ID = 'p'.repeat(81);
  f.publicKey.kid = f.config.vars.SIGNING_KEY_ID;
  assert(checkSetup(f.config, f).some(error => error.includes('SIGNING_KEY_ID is invalid')));
  const trusted = generateKeyPairSync('ed25519').publicKey.export({ format: 'jwk' }).x;
  f.config.vars.SIGNING_KEY_ID = 'looksy-staging-1';
  f.publicKey.kid = 'looksy-staging-1';
  f.publicKey.trustedKeys = { ['t'.repeat(81)]: Buffer.from(trusted, 'base64url').toString('base64') };
  assert(checkSetup(f.config, f).some(error => error.includes('trustedKeys contains an invalid key ID')));
});
