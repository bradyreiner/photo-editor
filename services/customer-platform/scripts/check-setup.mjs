// Offline configuration checks. Diagnostics contain field names, never values.
import { createPrivateKey, createPublicKey } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const serviceRoot = resolve(fileURLToPath(new URL('..', import.meta.url)));
const repoRoot = resolve(serviceRoot, '..', '..');
const CURRENT_APP_VERSION = readFileSync(resolve(repoRoot, 'VERSION'), 'utf8').trim();
const appMajor = version => {
  if (typeof version !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version)) return undefined;
  return Number(version.split('.')[0]);
};

const populated = value => typeof value === 'string' && value.trim().length > 0 &&
  !/[<>]|example|\.invalid|replace|placeholder/i.test(value);
const integer = (value, min, max) => (typeof value === 'number' || (typeof value === 'string' && /^\d+$/.test(value))) &&
  Number.isInteger(Number(value)) && Number(value) >= min && Number(value) <= max;

export function checkSetup(config, { environment = 'staging', publicKey, secrets, appVersion = CURRENT_APP_VERSION } = {}) {
  const errors = [];
  const requireField = (condition, field) => { if (!condition) errors.push(field); };
  requireField(['staging', 'production'].includes(environment), 'environment must be staging or production');
  const vars = config?.vars || {};
  let issuer;
  try { issuer = new URL(vars.ISSUER); } catch { /* reported below */ }
  requireField(issuer?.protocol === 'https:' && issuer.origin === vars.ISSUER && populated(vars.ISSUER) &&
    /^https:\/\/[A-Za-z0-9.-]+(:[0-9]+)?$/.test(vars.ISSUER), 'ISSUER requires an exact HTTPS origin');
  const db = Array.isArray(config?.d1_databases) ? config.d1_databases.find(binding => binding?.binding === 'DB') : undefined;
  requireField(/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(db?.database_id || '') &&
    db.database_id !== '00000000-0000-0000-0000-000000000000', 'DB requires a real D1 database ID');
  requireField(config?.assets?.run_worker_first === true, 'ASSETS must run the Worker before serving staff pages');
  requireField(config?.workers_dev === false && config?.preview_urls === false, 'disable workers_dev and preview_urls for staff access');
  requireField(/^[A-Za-z0-9_-]{1,80}$/.test(vars.SIGNING_KEY_ID || ''), 'SIGNING_KEY_ID is invalid');
  requireField(/^[a-z0-9-]+\.cloudflareaccess\.com$/.test(vars.ACCESS_TEAM_DOMAIN || ''), 'ACCESS_TEAM_DOMAIN is missing or invalid');
  requireField(populated(vars.ACCESS_AUDIENCE), 'ACCESS_AUDIENCE is missing');
  requireField(/^\d{1,20}$/.test(vars.LEMON_STORE_ID || ''), 'LEMON_STORE_ID is missing or invalid');
  requireField(integer(vars.TRIAL_DAYS, 1, 60), 'TRIAL_DAYS must be 1 to 60');
  requireField(integer(vars.LEASE_DAYS, 1, 30), 'LEASE_DAYS must be 1 to 30');
  requireField(vars.ALLOW_TEST_PURCHASES === (environment === 'production' ? 'false' : 'true'),
    'ALLOW_TEST_PURCHASES must match the selected environment');
  let policies;
  try { policies = JSON.parse(vars.PRODUCT_POLICY); } catch { /* reported below */ }
  requireField(policies && typeof policies === 'object' && !Array.isArray(policies) && Object.keys(policies).length > 0,
    'PRODUCT_POLICY needs explicit variant mappings');
  if (policies && typeof policies === 'object') {
    const selectedMajor = appMajor(appVersion);
    requireField(selectedMajor !== undefined, 'appVersion must be a semantic version in X.Y.Z form');
    const perpetualMajors = new Set();
    for (const [variant, policy] of Object.entries(policies)) {
      requireField(/^\d{1,20}$/.test(variant) && policy && /^\d{1,20}$/.test(policy.productId || '') &&
        populated(policy.plan) && policy.plan.length <= 40 && ['perpetual', 'subscription'].includes(policy.mode) &&
        integer(policy.major, 0, 1000) && integer(policy.deviceLimit, 1, 100) &&
        Array.isArray(policy.features) && policy.features.every(feature => ['editor', 'agent', 'beta'].includes(feature)),
      'PRODUCT_POLICY contains an invalid variant policy');
      if (policy?.mode === 'perpetual' && integer(policy.major, 0, 1000)) {
        perpetualMajors.add(Number(policy.major));
        requireField(selectedMajor === undefined || Number(policy.major) === selectedMajor,
          'perpetual PRODUCT_POLICY major must match the selected app version major');
      }
    }
    requireField(perpetualMajors.size <= 1, 'perpetual PRODUCT_POLICY variants must use one app major');
  }
  requireField(publicKey?.kid === vars.SIGNING_KEY_ID, 'desktop public key ID must match SIGNING_KEY_ID');
  requireField(typeof publicKey?.publicKeyBase64 === 'string' && /^[A-Za-z0-9+/]{43}=$/.test(publicKey.publicKeyBase64) &&
    Buffer.from(publicKey.publicKeyBase64, 'base64').length === 32, 'desktop public key must be raw Ed25519 base64');
  const trustedKeys = publicKey?.trustedKeys ?? {};
  requireField(trustedKeys && typeof trustedKeys === 'object' && !Array.isArray(trustedKeys) &&
    Object.keys(trustedKeys).length <= 4, 'desktop trustedKeys must be an object with at most four additional keys');
  if (trustedKeys && typeof trustedKeys === 'object' && !Array.isArray(trustedKeys)) {
    for (const [kid, key] of Object.entries(trustedKeys)) {
      const validKey = typeof key === 'string' && /^[A-Za-z0-9+/]{43}=$/.test(key) &&
        Buffer.from(key, 'base64').length === 32;
      requireField(/^[A-Za-z0-9_-]{1,80}$/.test(kid) && validKey,
        'desktop trustedKeys contains an invalid key ID or raw Ed25519 public key');
      requireField(kid !== publicKey?.kid || key === publicKey?.publicKeyBase64,
        'desktop trustedKeys conflicts with the primary signing key');
    }
  }
  if (!secrets) errors.push('Worker secrets have not been checked; supply --secrets-file');
  else {
    requireField(populated(secrets.LEMON_API_KEY), 'LEMON_API_KEY is missing');
    for (const name of ['LEMON_WEBHOOK_SECRET', 'RATE_LIMIT_SECRET']) {
      requireField(populated(secrets[name]) && secrets[name].length >= 32, `${name} needs at least 32 characters`);
    }
    requireField(secrets.LEMON_WEBHOOK_SECRET !== secrets.RATE_LIMIT_SECRET, 'webhook and rate-limit secrets must differ');
    try {
      const privateKey = createPrivateKey(secrets.ENTITLEMENT_PRIVATE_KEY);
      const derived = createPublicKey(privateKey).export({ format: 'jwk' });
      requireField(privateKey.asymmetricKeyType === 'ed25519' && Buffer.from(derived.x, 'base64url').toString('base64') === publicKey?.publicKeyBase64,
        'ENTITLEMENT_PRIVATE_KEY must match the desktop public key');
    } catch { errors.push('ENTITLEMENT_PRIVATE_KEY is missing or invalid'); }
  }
  return [...new Set(errors)];
}

async function main(args) {
  if (args.includes('--help')) {
    console.log('Usage: node scripts/check-setup.mjs --config file.json --public-key-file file.public.json [--secrets-file file.json] [--environment staging|production] [--app-version X.Y.Z]\nOffline check only; does not verify deployed secrets, Access/MFA, billing, or database enrollment.');
    return;
  }
  const options = {};
  for (let i = 0; i < args.length; i += 2) {
    if (!['--config', '--public-key-file', '--secrets-file', '--environment', '--app-version'].includes(args[i]) || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error('Invalid arguments; use --help');
    if (options[args[i]]) throw new Error('Duplicate argument');
    options[args[i]] = args[i + 1];
  }
  const read = async path => JSON.parse(await readFile(path, 'utf8'));
  const config = await read(options['--config'] || 'wrangler.jsonc');
  const publicKey = options['--public-key-file'] ? await read(options['--public-key-file']) : undefined;
  const secretsPath = options['--secrets-file'];
  const secrets = secretsPath ? await read(secretsPath) : undefined;
  const errors = checkSetup(config, { publicKey, secrets, environment: options['--environment'] || 'staging', appVersion: options['--app-version'] || CURRENT_APP_VERSION });
  if (secretsPath && process.platform !== 'win32' && ((await stat(secretsPath)).mode & 0o077)) errors.push('secrets file must restrict group and other access (chmod 600)');
  for (const error of errors) console.log(`Missing or invalid: ${error}`);
  console.log(errors.length ? `Setup needs ${errors.length} corrections. No values were printed.` :
    'Offline configuration and signing-key checks passed. Live Access/MFA, staff enrollment, provider delivery, database migrations and activation still need verification.');
  process.exitCode = errors.length ? 1 : 0;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).catch(() => { console.error('Setup check could not read the supplied files; use --help. No values were printed.'); process.exitCode = 1; });
}
