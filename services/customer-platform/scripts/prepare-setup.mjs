#!/usr/bin/env node
// Prepare a local staging configuration and keys. This script never calls Cloudflare.
import { createPrivateKey, createPublicKey, generateKeyPairSync, randomBytes } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { mkdir, open, readFile, realpath, link, unlink, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const serviceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const repoRoot = path.resolve(serviceRoot, '..', '..');
const BASE_CONFIG = path.join(serviceRoot, 'wrangler.jsonc');
const VERSION_FILE = path.join(repoRoot, 'VERSION');

function fail(message) { throw new Error(message); }
function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) && Object.getPrototypeOf(value) === Object.prototype;
}
function boundedInteger(value, min, max, label) {
  if (!Number.isInteger(value) || value < min || value > max) fail(`${label} must be an integer from ${min} to ${max}`);
  return value;
}
function appMajorFromVersion(version) {
  if (typeof version !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version)) {
    fail('appVersion must be a semantic version in X.Y.Z form');
  }
  return Number(version.split('.')[0]);
}
function requiredString(value, label, max = 200) {
  if (typeof value !== 'string' || value.length === 0 || value.length > max || value.trim() !== value) fail(`${label} must be a non-empty string`);
  return value;
}
function rejectPlaceholder(value, label) {
  if (/(?:\.invalid\b|example|placeholder|replace[-_ ]?me|changeme|00000000)/i.test(value)) fail(`${label} cannot be a placeholder`);
}
function contained(parent, child) {
  const rel = path.relative(parent, child);
  return rel === '' || (rel !== '..' && !rel.startsWith(`..${path.sep}`) && !path.isAbsolute(rel));
}

export function validateSetupInput(input, { appVersion } = {}) {
  if (!isPlainObject(input)) fail('Input must be a JSON object');
  const issuer = requiredString(input.issuer, 'issuer', 300);
  let parsedIssuer;
  try { parsedIssuer = new URL(issuer); } catch { fail('issuer must be an exact HTTPS origin'); }
  if (parsedIssuer.protocol !== 'https:' || parsedIssuer.username || parsedIssuer.password ||
      parsedIssuer.pathname !== '/' || parsedIssuer.search || parsedIssuer.hash || parsedIssuer.origin !== issuer) {
    fail('issuer must be an exact HTTPS origin with no path, query, fragment, credentials, or trailing slash');
  }
  if (!/^https:\/\/[A-Za-z0-9.-]+(?::[0-9]+)?$/.test(issuer)) fail('issuer must match the desktop HTTPS origin format (ASCII DNS name and optional port)');
  rejectPlaceholder(issuer, 'issuer');

  const databaseId = requiredString(input.databaseId, 'databaseId', 36);
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(databaseId) || /^0{8}-0{4}-0{4}-0{4}-0{12}$/i.test(databaseId)) {
    fail('databaseId must be a real D1 database UUID');
  }
  const accessTeamDomain = requiredString(input.accessTeamDomain, 'accessTeamDomain', 200).toLowerCase();
  if (!/^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.cloudflareaccess\.com$/.test(accessTeamDomain)) fail('accessTeamDomain must be a Cloudflare Access team domain');
  rejectPlaceholder(accessTeamDomain, 'accessTeamDomain');
  const accessAudience = requiredString(input.accessAudience, 'accessAudience', 256);
  if (!/^[A-Za-z0-9_-]{16,256}$/.test(accessAudience)) fail('accessAudience must be a Cloudflare Access audience tag');
  rejectPlaceholder(accessAudience, 'accessAudience');
  const storeId = requiredString(input.storeId, 'storeId', 20);
  if (!/^[1-9][0-9]{0,19}$/.test(storeId)) fail('storeId must be a positive numeric Lemon Squeezy store ID');
  const signingKeyId = requiredString(input.signingKeyId, 'signingKeyId', 80);
  if (!/^[A-Za-z0-9_-]+$/.test(signingKeyId)) fail('signingKeyId may contain only letters, numbers, underscore, and hyphen');

  const policyMajor = appMajorFromVersion(appVersion ?? readFileSync(VERSION_FILE, 'utf8').trim());

  if (!isPlainObject(input.productPolicy)) fail('productPolicy must be an object keyed by variant ID');
  const entries = Object.entries(input.productPolicy);
  if (entries.length < 2) fail('productPolicy must include at least one perpetual and one subscription variant');
  let hasPerpetual = false, hasSubscription = false;
  const productPolicy = {};
  for (const [variantId, raw] of entries) {
    if (!/^[1-9][0-9]{0,19}$/.test(variantId)) fail('productPolicy keys must be positive numeric variant IDs');
    if (!isPlainObject(raw)) fail(`productPolicy variant ${variantId} must be an object`);
    const productId = requiredString(raw.productId, `productPolicy ${variantId} productId`, 20);
    if (!/^[1-9][0-9]{0,19}$/.test(productId)) fail(`productPolicy ${variantId} productId must be numeric`);
    const plan = requiredString(raw.plan, `productPolicy ${variantId} plan`, 40);
    rejectPlaceholder(plan, `productPolicy ${variantId} plan`);
    if (raw.mode !== 'perpetual' && raw.mode !== 'subscription') fail(`productPolicy ${variantId} mode must be perpetual or subscription`);
    if (raw.mode === 'perpetual') hasPerpetual = true;
    else hasSubscription = true;
    const major = boundedInteger(raw.major, 0, 1000, `productPolicy ${variantId} major`);
    if (raw.mode === 'perpetual' && major !== policyMajor) {
      fail(`productPolicy ${variantId} perpetual major must match app version major ${policyMajor}`);
    }
    if (!Array.isArray(raw.features) || raw.features.length === 0 || !raw.features.every(feature => ['editor', 'agent', 'beta'].includes(feature)) || new Set(raw.features).size !== raw.features.length) {
      fail(`productPolicy ${variantId} features must be a non-empty unique list of editor, agent, and beta`);
    }
    const deviceLimit = boundedInteger(raw.deviceLimit, 1, 100, `productPolicy ${variantId} deviceLimit`);
    productPolicy[variantId] = { productId, plan, mode: raw.mode, major, features: [...raw.features], deviceLimit };
  }
  if (!hasPerpetual || !hasSubscription) fail('productPolicy must include at least one perpetual and one subscription variant');

  const trustedKeysInput = input.trustedKeys ?? {};
  if (!isPlainObject(trustedKeysInput)) fail('trustedKeys must be an object keyed by signing key ID');
  const trustedKeys = {};
  const trustedEntries = Object.entries(trustedKeysInput);
  if (trustedEntries.length > 4) fail('trustedKeys may include at most four additional public keys');
  for (const [kid, publicKeyBase64] of trustedEntries) {
    if (!/^[A-Za-z0-9_-]{1,80}$/.test(kid)) fail('trustedKeys contains an invalid key ID');
    if (typeof publicKeyBase64 !== 'string' || !/^[A-Za-z0-9+/]{43}=$/.test(publicKeyBase64) ||
        Buffer.from(publicKeyBase64, 'base64').length !== 32) {
      fail('trustedKeys values must be raw Ed25519 public keys encoded as base64');
    }
    trustedKeys[kid] = publicKeyBase64;
  }

  return {
    issuer, databaseId, accessTeamDomain, accessAudience, storeId, signingKeyId, productPolicy, trustedKeys,
    trialDays: boundedInteger(input.trialDays, 1, 60, 'trialDays'),
    leaseDays: boundedInteger(input.leaseDays, 1, 30, 'leaseDays'),
  };
}

async function writeExclusive(target, contents, mode, temporaryFiles, createdFiles) {
  const temporary = path.join(path.dirname(target), `.${path.basename(target)}.${process.pid}.${randomBytes(8).toString('hex')}.tmp`);
  const handle = await open(temporary, 'wx', mode);
  temporaryFiles.add(temporary);
  try {
    await handle.writeFile(contents, 'utf8');
    await handle.sync();
  } finally { await handle.close(); }
  // A hard link is exclusive: unlike rename, it cannot replace an existing destination.
  await link(temporary, target);
  createdFiles.add(target);
}

function cmakeString(value) {
  return `"${value.replaceAll('\\', '\\\\').replaceAll('"', '\\"').replaceAll(';', '\\;')}"`;
}

function setupArtifacts(config, keyPair, outputPath) {
  const publicJwk = createPublicKey(keyPair.privateKey).export({ format: 'jwk' });
  const publicKey = Buffer.from(publicJwk.x, 'base64url').toString('base64');
  const trustedKeys = { ...config.input.trustedKeys };
  if (trustedKeys[config.input.signingKeyId] !== undefined && trustedKeys[config.input.signingKeyId] !== publicKey) {
    fail('trustedKeys cannot conflict with the generated primary signing key');
  }
  delete trustedKeys[config.input.signingKeyId];
  const publicConfig = { kid: config.input.signingKeyId, publicKeyBase64: publicKey, trustedKeys };
  const wrangler = JSON.parse(JSON.stringify(config.baseConfig));
  wrangler.name = 'looksy-customers-staging';
  wrangler.d1_databases = wrangler.d1_databases.map(binding => ({
    ...binding, database_name: 'looksy-customers-staging', database_id: config.input.databaseId,
  }));
  wrangler.vars = {
    ...wrangler.vars,
    ISSUER: config.input.issuer,
    SIGNING_KEY_ID: config.input.signingKeyId,
    LEMON_STORE_ID: config.input.storeId,
    PRODUCT_POLICY: JSON.stringify(config.input.productPolicy),
    ALLOW_TEST_PURCHASES: 'true',
    TRIAL_DAYS: String(config.input.trialDays),
    LEASE_DAYS: String(config.input.leaseDays),
    ACCESS_TEAM_DOMAIN: config.input.accessTeamDomain,
    ACCESS_AUDIENCE: config.input.accessAudience,
  };
  const privatePem = keyPair.privateKey.export({ format: 'pem', type: 'pkcs8' });
  const secrets = {
    ENTITLEMENT_PRIVATE_KEY: privatePem,
    LEMON_WEBHOOK_SECRET: randomBytes(32).toString('base64url'),
    RATE_LIMIT_SECRET: randomBytes(32).toString('base64url'),
  };
  const cmake = [
    `set(LOOKSY_CUSTOMER_API_URL ${cmakeString(config.input.issuer)} CACHE STRING "" FORCE)`,
    `set(LOOKSY_ENTITLEMENT_PUBLIC_KEY ${cmakeString(publicKey)} CACHE STRING "" FORCE)`,
    `set(LOOKSY_ENTITLEMENT_KEY_ID ${cmakeString(config.input.signingKeyId)} CACHE STRING "" FORCE)`,
    `set(LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON ${cmakeString(JSON.stringify(trustedKeys))} CACHE STRING "" FORCE)`,
    `set(LOOKSY_CUSTOMER_POLICY_MAJOR ${config.appMajor} CACHE STRING "" FORCE)`,
    '',
  ].join('\n');
  return {
    files: [
      { path: path.join(config.keyDirectory, `${config.input.signingKeyId}.pem`), contents: privatePem, mode: 0o600 },
      { path: path.join(config.keyDirectory, `${config.input.signingKeyId}.public.json`), contents: `${JSON.stringify(publicConfig, null, 2)}\n`, mode: 0o600 },
      { path: path.join(config.keyDirectory, 'secrets.json'), contents: `${JSON.stringify(secrets, null, 2)}\n`, mode: 0o600 },
      { path: path.join(path.dirname(outputPath), 'desktop-customer.local.cmake'), contents: cmake, mode: 0o600 },
      { path: outputPath, contents: `${JSON.stringify(wrangler, null, 2)}\n`, mode: 0o600 },
    ],
    publicKey,
  };
}

export async function prepareSetup({ input, outputPath, keyDirectory, serviceDirectory = serviceRoot, appVersion }) {
  const appVersionText = appVersion ?? (await readFile(VERSION_FILE, 'utf8')).trim();
  const appMajor = appMajorFromVersion(appVersionText);
  const validated = validateSetupInput(input, { appVersion: appVersionText });
  if (typeof outputPath !== 'string' || !outputPath) fail('outputPath is required');
  if (typeof keyDirectory !== 'string' || !keyDirectory) fail('keyDirectory is required');
  const service = await realpath(serviceDirectory);
  const outputRequested = path.resolve(outputPath);
  const outputParent = await realpath(path.dirname(outputRequested)).catch(() => fail('outputPath parent must already exist'));
  const output = path.join(outputParent, path.basename(outputRequested));
  if (outputParent !== service || !output.endsWith('.local.jsonc')) fail('outputPath must be a .local.jsonc file directly inside services/customer-platform');
  if (!contained(service, output) || output === BASE_CONFIG) fail('outputPath must be a new staging config inside the customer-platform service');
  const keyResolved = path.resolve(keyDirectory);
  const keyParent = await realpath(path.dirname(keyResolved)).catch(() => fail('keyDirectory parent must already exist'));
  const keyCanonical = path.join(keyParent, path.basename(keyResolved));
  if (contained(repoRoot, keyCanonical)) fail('keyDirectory must be outside the repository checkout');

  const existingOutput = await stat(output).catch(error => error.code === 'ENOENT' ? null : fail('Unable to inspect output path'));
  if (existingOutput) fail('One or more output files already exist; no files were changed');
  const baseConfig = JSON.parse(await readFile(BASE_CONFIG, 'utf8'));
  if (!Array.isArray(baseConfig.d1_databases) || baseConfig.d1_databases.length !== 1 || !isPlainObject(baseConfig.assets)) fail('Staging template configuration is not in the expected shape');

  await mkdir(keyCanonical, { recursive: true, mode: 0o700 });
  const keyDirInfo = await stat(keyCanonical);
  if (!keyDirInfo.isDirectory() || (keyDirInfo.mode & 0o077) !== 0) fail('keyDirectory must be a private directory with mode 0700');
  const realKeyDirectory = await realpath(keyCanonical);
  if (contained(repoRoot, realKeyDirectory)) fail('keyDirectory must resolve outside the repository checkout');
  const { privateKey } = generateKeyPairSync('ed25519');
  // Verify export/import before writing any generated material.
  const imported = createPrivateKey(privateKey.export({ format: 'pem', type: 'pkcs8' }));
  const outputs = setupArtifacts({ input: validated, keyDirectory: realKeyDirectory, baseConfig, appMajor }, { privateKey: imported }, output);
  const tempFiles = new Set(), createdFiles = new Set();
  try {
    for (const file of outputs.files) await writeExclusive(file.path, file.contents, file.mode, tempFiles, createdFiles);
    for (const temporary of tempFiles) await unlink(temporary);
    return {
      configPath: output,
      keyDirectory: realKeyDirectory,
      privateKeyPath: outputs.files[0].path,
      publicKeyPath: outputs.files[1].path,
      secretsPath: outputs.files[2].path,
      desktopConfigPath: outputs.files[3].path,
      pendingOperatorSecret: 'LEMON_API_KEY',
    };
  } catch (error) {
    await Promise.allSettled([...createdFiles].map(file => unlink(file)));
    await Promise.allSettled([...tempFiles].map(file => unlink(file)));
    throw error;
  }
}

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index++) {
    const flag = argv[index];
    if (!['--input', '--output', '--key-dir', '--app-version'].includes(flag) || args[flag] || !argv[index + 1] || argv[index + 1].startsWith('--')) fail('Usage: node scripts/prepare-setup.mjs --input staging.json --output services/customer-platform/wrangler.staging.local.jsonc --key-dir /outside/checkout/keys [--app-version X.Y.Z]');
    args[flag] = argv[++index];
  }
  if (!args['--input'] || !args['--output'] || !args['--key-dir']) fail('Usage: node scripts/prepare-setup.mjs --input staging.json --output services/customer-platform/wrangler.staging.local.jsonc --key-dir /outside/checkout/keys [--app-version X.Y.Z]');
  return args;
}

async function main(argv) {
  if (argv.length === 1 && argv[0] === '--help') {
    console.log('Usage (from services/customer-platform): node scripts/prepare-setup.mjs --input setup.local.json --output wrangler.staging.local.jsonc --key-dir /outside/checkout/keys [--app-version X.Y.Z]\nPrepares staging files only. Requires real identifiers in setup input. Never overwrites files or contacts Cloudflare.');
    return;
  }
  const args = parseArgs(argv);
  let input;
  try { input = JSON.parse(await readFile(path.resolve(args['--input']), 'utf8')); }
  catch { fail('Unable to read --input as JSON'); }
  const result = await prepareSetup({ input, outputPath: path.resolve(args['--output']), keyDirectory: path.resolve(args['--key-dir']), appVersion: args['--app-version'] });
  console.log(`Prepared local staging configuration: ${result.configPath}`);
  console.log(`Generated private keys and secret file in: ${result.keyDirectory}`);
  console.log(`Generated public desktop CMake configuration: ${result.desktopConfigPath}`);
  console.log('Set LEMON_API_KEY from the Lemon Squeezy dashboard before staging use. No Cloudflare resources were changed.');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).catch(error => { console.error(`setup preparation failed: ${error.message}`); process.exitCode = 1; });
}
