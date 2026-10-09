import assert from 'node:assert/strict';
import { mkdtemp, mkdir, readFile, rm, stat } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { createPublicKey, generateKeyPairSync, verify } from 'node:crypto';
import { prepareSetup, validateSetupInput } from '../prepare-setup.mjs';
import { checkSetup } from '../check-setup.mjs';

const validInput = () => ({
  issuer: 'https://accounts.looksy.test',
  databaseId: '123e4567-e89b-12d3-a456-426614174000',
  accessTeamDomain: 'looksy.cloudflareaccess.com',
  accessAudience: 'a'.repeat(32),
  storeId: '12345',
  signingKeyId: 'looksy-staging-1',
  productPolicy: {
    '101': { productId: '10', plan: 'studio', mode: 'perpetual', major: 0, features: ['editor', 'agent'], deviceLimit: 2 },
    '102': { productId: '11', plan: 'studio-monthly', mode: 'subscription', major: 0, features: ['editor', 'agent'], deviceLimit: 2 },
  },
  trialDays: 14,
  leaseDays: 7,
});

test('validates exact HTTPS origins and rejects placeholders or non-desktop hosts', () => {
  assert.equal(validateSetupInput(validInput()).issuer, 'https://accounts.looksy.test');
  for (const issuer of [
    'http://accounts.looksy.test',
    'https://accounts.looksy.test/',
    'https://accounts.looksy.test/v1',
    'https://accounts.looksy.test?x=1',
    'https://accounts.looksy.invalid',
    'https://[2001:db8::1]',
    'https://accounts_looksy.example',
  ]) assert.throws(() => validateSetupInput({ ...validInput(), issuer }));
});

test('requires real identifiers, both paid modes, and bounded policy values', () => {
  assert.throws(() => validateSetupInput({ ...validInput(), databaseId: '00000000-0000-0000-0000-000000000000' }));
  assert.throws(() => validateSetupInput({ ...validInput(), accessAudience: 'placeholder' }));
  assert.throws(() => validateSetupInput({ ...validInput(), storeId: '0' }));
  assert.throws(() => validateSetupInput({ ...validInput(), productPolicy: { '101': validInput().productPolicy['101'] } }));
  const productPolicy = validInput().productPolicy;
  assert.throws(() => validateSetupInput({ ...validInput(), productPolicy: {
    ...productPolicy,
    '102': { ...productPolicy['102'], features: ['admin'] },
  } }));
  assert.throws(() => validateSetupInput({ ...validInput(), productPolicy: {
    ...productPolicy,
    '102': { ...productPolicy['102'], deviceLimit: 0 },
  } }));
  assert.throws(() => validateSetupInput({ ...validInput(), trialDays: 61 }));
  assert.throws(() => validateSetupInput({ ...validInput(), leaseDays: 31 }));
  assert.throws(() => validateSetupInput({ ...validInput(), signingKeyId: 'k'.repeat(81) }), /signingKeyId/);
});

test('requires perpetual license majors to match the repository app major unless an isolated version is selected', () => {
  const input = validInput();
  input.productPolicy['101'].major = 1;
  assert.throws(() => validateSetupInput(input), /must match app version major 0/);
  assert.equal(validateSetupInput(input, { appVersion: '1.0.0' }).productPolicy['101'].major, 1);
  assert.throws(() => validateSetupInput(input, { appVersion: 'future' }), /semantic version/);
});

test('validates and emits optional public signing-key overlap configuration', async t => {
  const oldPublic = generateKeyPairSync('ed25519').publicKey.export({ format: 'jwk' }).x;
  const trustedKeys = { 'looksy-staging-old': Buffer.from(oldPublic, 'base64url').toString('base64') };
  const input = { ...validInput(), trustedKeys };
  assert.deepEqual(validateSetupInput(input).trustedKeys, trustedKeys);
  assert.throws(() => validateSetupInput({ ...input, trustedKeys: { invalid: 'bad' } }), /raw Ed25519 public keys/);
  assert.throws(() => validateSetupInput({ ...input, trustedKeys: { ['k'.repeat(81)]: trustedKeys['looksy-staging-old'] } }), /invalid key ID/);
  assert.throws(() => validateSetupInput({ ...input, trustedKeys: Object.fromEntries(Array.from({ length: 5 }, (_, index) => [`old-${index}`, trustedKeys['looksy-staging-old']])) }), /at most four/);

  const temporary = await mkdtemp(path.join(os.tmpdir(), 'looksy-setup-keys-'));
  t.after(() => rm(temporary, { recursive: true, force: true }));
  const service = path.join(temporary, 'service');
  const keyParent = path.join(temporary, 'private');
  await mkdir(service);
  await mkdir(keyParent, { mode: 0o700 });
  const result = await prepareSetup({
    input, outputPath: path.join(service, 'wrangler.staging.local.jsonc'),
    keyDirectory: path.join(keyParent, 'keys'), serviceDirectory: service,
  });
  const publicInfo = JSON.parse(await readFile(result.publicKeyPath, 'utf8'));
  const cmake = await readFile(result.desktopConfigPath, 'utf8');
  assert.deepEqual(publicInfo.trustedKeys, trustedKeys);
  assert.match(cmake, /LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON/);
  assert.match(cmake, /LOOKSY_CUSTOMER_POLICY_MAJOR 0/);
});

test('generates a staging-only config, private secrets, matching key pair, and public CMake cache', async t => {
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'looksy-setup-'));
  t.after(() => rm(temporary, { recursive: true, force: true }));
  const service = path.join(temporary, 'service');
  const keyParent = path.join(temporary, 'private');
  await mkdir(service);
  await mkdir(keyParent, { mode: 0o700 });
  const outputPath = path.join(service, 'wrangler.staging.local.jsonc');
  const keyDirectory = path.join(keyParent, 'keys');

  const result = await prepareSetup({ input: validInput(), outputPath, keyDirectory, serviceDirectory: service });
  const wrangler = JSON.parse(await readFile(outputPath, 'utf8'));
  const publicInfo = JSON.parse(await readFile(result.publicKeyPath, 'utf8'));
  const secrets = JSON.parse(await readFile(result.secretsPath, 'utf8'));
  const cmake = await readFile(result.desktopConfigPath, 'utf8');
  assert.equal(wrangler.name, 'looksy-customers-staging');
  assert.equal(wrangler.vars.ALLOW_TEST_PURCHASES, 'true');
  assert.equal(wrangler.vars.ISSUER, validInput().issuer);
  assert.equal(wrangler.vars.SIGNING_KEY_ID, validInput().signingKeyId);
  assert.equal(wrangler.d1_databases[0].database_id, validInput().databaseId);
  assert.equal(wrangler.d1_databases[0].binding, 'DB');
  assert.equal(wrangler.assets.directory, './public');
  assert.equal(publicInfo.kid, validInput().signingKeyId);
  assert.equal(Object.keys(secrets).sort().join(','), 'ENTITLEMENT_PRIVATE_KEY,LEMON_WEBHOOK_SECRET,RATE_LIMIT_SECRET');
  assert.ok(secrets.LEMON_WEBHOOK_SECRET.length >= 32);
  assert.ok(secrets.RATE_LIMIT_SECRET.length >= 32);
  assert.doesNotMatch(cmake, /LEMON|PRIVATE|WEBHOOK|RATE_LIMIT/);
  assert.match(cmake, /LOOKSY_CUSTOMER_API_URL/);
  assert.match(cmake, /LOOKSY_ENTITLEMENT_PUBLIC_KEY/);
  assert.match(cmake, /LOOKSY_ENTITLEMENT_KEY_ID/);
  assert.equal(result.pendingOperatorSecret, 'LEMON_API_KEY');
  assert.deepEqual(checkSetup(wrangler, { publicKey: publicInfo, secrets }), ['LEMON_API_KEY is missing']);
  assert.deepEqual(checkSetup(wrangler, { publicKey: publicInfo, secrets: { ...secrets, LEMON_API_KEY: 'synthetic-provider-credential' } }), []);

  const privatePath = result.privateKeyPath;
  const privatePem = await readFile(privatePath, 'utf8');
  const privateKey = (await import('node:crypto')).createPrivateKey(privatePem);
  const publicKey = createPublicKey(privateKey);
  const derived = publicKey.export({ format: 'jwk' }).x;
  assert.equal(Buffer.from(derived, 'base64url').toString('base64'), publicInfo.publicKeyBase64);
  const mode = (await stat(privatePath)).mode & 0o777;
  assert.equal(mode, 0o600);
  assert.equal((await stat(result.secretsPath)).mode & 0o777, 0o600);
  assert.equal((await stat(result.configPath)).mode & 0o777, 0o600);
  assert.equal((await stat(result.keyDirectory)).mode & 0o077, 0);
  // The configured public key is valid Ed25519 material; verification exercises its key type.
  assert.equal(publicKey.asymmetricKeyType, 'ed25519');
  assert.equal(verify(null, Buffer.from('fixture'), publicKey, Buffer.alloc(64)), false);
});

test('rejects repeat generation and preserves existing output files', async t => {
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'looksy-setup-existing-'));
  t.after(() => rm(temporary, { recursive: true, force: true }));
  const service = path.join(temporary, 'service');
  const keyParent = path.join(temporary, 'private');
  await mkdir(service);
  await mkdir(keyParent, { mode: 0o700 });
  const outputPath = path.join(service, 'wrangler.staging.local.jsonc');
  const keyDirectory = path.join(keyParent, 'keys');
  await prepareSetup({ input: validInput(), outputPath, keyDirectory, serviceDirectory: service });
  const originalConfig = await readFile(outputPath, 'utf8');
  const originalKey = await readFile(path.join(keyDirectory, `${validInput().signingKeyId}.pem`), 'utf8');
  await assert.rejects(() => prepareSetup({ input: validInput(), outputPath, keyDirectory, serviceDirectory: service }), /already exist/);
  assert.equal(await readFile(outputPath, 'utf8'), originalConfig);
  assert.equal(await readFile(path.join(keyDirectory, `${validInput().signingKeyId}.pem`), 'utf8'), originalKey);
});

test('requires the private key directory to be outside the checkout', async t => {
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'looksy-setup-path-'));
  t.after(() => rm(temporary, { recursive: true, force: true }));
  const service = path.join(temporary, 'service');
  await mkdir(service);
  await assert.rejects(() => prepareSetup({
    input: validInput(),
    outputPath: path.join(service, 'wrangler.staging.local.jsonc'),
    keyDirectory: path.join(path.dirname(new URL(import.meta.url).pathname), 'generated-test-keys'),
    serviceDirectory: service,
  }), /outside the repository checkout/);
});
