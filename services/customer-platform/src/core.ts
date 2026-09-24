import { importPKCS8, SignJWT } from 'jose';

export type ServiceEnv = Env & {
  ENTITLEMENT_PRIVATE_KEY: string;
  LEMON_API_KEY: string;
  LEMON_WEBHOOK_SECRET: string;
  RATE_LIMIT_SECRET: string;
};
export class ApiError extends Error {
  constructor(public status: number, public code: string) { super(code); }
}
export const now = () => Math.floor(Date.now() / 1000);
export const DAY = 86400;
export const json = (value: unknown, status = 200) => Response.json(value, {
  status, headers: { 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' },
});
export function object(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new ApiError(400, 'invalid_object');
  return value as Record<string, unknown>;
}
export function str(value: unknown, max = 200): string {
  if (typeof value !== 'string' || !value.length || value.length > max) throw new ApiError(400, 'invalid_string');
  return value;
}
export function id(value: unknown): string {
  const result = String(value);
  if (!/^\d{1,20}$/.test(result)) throw new ApiError(400, 'invalid_provider_id');
  return result;
}
export function boundedInt(value: unknown, min: number, max: number): number {
  const n = Number(value);
  if (!Number.isInteger(n) || n < min || n > max) throw new ApiError(503, 'invalid_policy');
  return n;
}
export async function boundedText(source: Request | Response, max = 32768): Promise<string> {
  if (Number(source.headers.get('content-length')) > max) throw new ApiError(413, 'body_too_large');
  const reader = source.body?.getReader();
  if (!reader) return '';
  const chunks: Uint8Array[] = [];
  let length = 0;
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > max) { await reader.cancel(); throw new ApiError(413, 'body_too_large'); }
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  const result = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.length; }
  return new TextDecoder('utf-8', { fatal: true, ignoreBOM: false }).decode(result);
}
export async function body(request: Request): Promise<Record<string, unknown>> {
  if (request.headers.get('content-type')?.split(';')[0] !== 'application/json') throw new ApiError(415, 'json_required');
  try { return object(JSON.parse(await boundedText(request))); }
  catch (error) { if (error instanceof ApiError) throw error; throw new ApiError(400, 'invalid_json'); }
}
export async function hash(value: string): Promise<string> {
  return hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value)));
}
export function hex(value: ArrayBuffer): string {
  return [...new Uint8Array(value)].map(x => x.toString(16).padStart(2, '0')).join('');
}
export async function hmacKey(secret: string): Promise<CryptoKey> {
  if (!secret || secret.length < 32) throw new ApiError(503, 'secret_not_configured');
  return crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
}
export async function rateLimit(request: Request, env: ServiceEnv, scope: string, limit: number): Promise<void> {
  // Daily HMAC avoids storing IP addresses or a stable unsalted IP hash.
  const time = now();
  const ip = request.headers.get('CF-Connecting-IP') || 'local';
  const digest = hex(await crypto.subtle.sign('HMAC', await hmacKey(env.RATE_LIMIT_SECRET),
    new TextEncoder().encode(`${Math.floor(time / DAY)}:${ip}`)));
  const key = `${scope}:${Math.floor(time / 60)}:${digest}`;
  const row = await env.DB.prepare(`INSERT INTO rate_buckets(id,count,expires_at) VALUES(?,1,?)
    ON CONFLICT(id) DO UPDATE SET count=count+1 RETURNING count`).bind(key, time + 120).first<{count: number}>();
  if (!row || row.count > limit) throw new ApiError(429, 'rate_limited');
}
export interface Installation {
  id: string; credential_hash: string; customer_id: string; license_id: string | null;
  trial_ends_at: number; revoked: number;
}
export interface License {
  id: string; customer_id: string; order_id: string; order_item_id: string; variant_id: string;
  plan: string; mode: 'perpetual' | 'subscription'; major: number; features: string;
  device_limit: number; status: string; expires_at: number | null; checked_at: number; blocked: number;
}
export interface ProductPolicy {
  productId: string; plan: string; mode: 'perpetual' | 'subscription'; major: number;
  features: string[]; deviceLimit: number;
}
export function productPolicy(env: ServiceEnv, variant: string): ProductPolicy {
  const raw = object(object(JSON.parse(env.PRODUCT_POLICY))[variant]);
  const mode = raw.mode;
  if (mode !== 'perpetual' && mode !== 'subscription') throw new ApiError(503, 'invalid_policy');
  const features = raw.features;
  if (!Array.isArray(features) || !features.every(f => ['editor', 'agent', 'beta'].includes(f))) throw new ApiError(503, 'invalid_policy');
  return { productId: id(raw.productId), plan: str(raw.plan, 40), mode,
    major: boundedInt(raw.major, 0, 1000), features, deviceLimit: boundedInt(raw.deviceLimit, 1, 100) };
}
export function timestamp(value: unknown): number | null {
  if (value === null || value === undefined) return null;
  const time = Date.parse(str(value));
  if (!Number.isFinite(time)) throw new ApiError(502, 'invalid_provider_date');
  return Math.floor(time / 1000);
}
export async function authenticate(request: Request, env: ServiceEnv): Promise<Installation> {
  const token = request.headers.get('Authorization')?.replace(/^Bearer /, '');
  if (!token || !/^[A-Za-z0-9_-]{43}$/.test(token)) throw new ApiError(401, 'authentication_required');
  const device = await env.DB.prepare('SELECT * FROM installations WHERE credential_hash=?').bind(await hash(token)).first<Installation>();
  if (!device || device.revoked) throw new ApiError(401, 'device_revoked');
  return device;
}
export async function issueLease(env: ServiceEnv, device: Installation): Promise<string> {
  const time = now();
  let plan = 'trial', mode = 'trial', major = -1, features = ['editor', 'agent'];
  let expires = device.trial_ends_at;
  if (device.license_id) {
    const license = await env.DB.prepare('SELECT * FROM licenses WHERE id=?').bind(device.license_id).first<License>();
    if (!license || license.blocked || !['active', 'inactive'].includes(license.status)) throw new ApiError(403, 'license_inactive');
    plan = license.plan; mode = license.mode; major = license.major;
    let deviceLimit = license.device_limit;
    features = JSON.parse(license.features);
    // Never extend offline grace by repeatedly refreshing stale provider state.
    expires = license.checked_at + boundedInt(env.LEASE_DAYS, 1, 30) * DAY;
    if (license.expires_at !== null) expires = Math.min(expires, license.expires_at);
    if (mode === 'subscription') {
      const subscription = await env.DB.prepare('SELECT * FROM subscriptions WHERE order_item_id=?')
        .bind(license.order_item_id).first<{status: string; ends_at: number | null; checked_at: number; variant_id: string}>();
      if (!subscription || !['active', 'cancelled', 'on_trial', 'past_due'].includes(subscription.status)) throw new ApiError(403, 'subscription_inactive');
      // Plan changes follow the currently verified subscription variant.
      const policy = productPolicy(env, subscription.variant_id);
      if (policy.mode !== 'subscription') throw new ApiError(503, 'invalid_policy');
      plan = policy.plan; features = policy.features; major = policy.major;
      deviceLimit = policy.deviceLimit;
      expires = Math.min(expires, subscription.checked_at + boundedInt(env.LEASE_DAYS, 1, 30) * DAY);
      if (subscription.ends_at !== null) expires = Math.min(expires, subscription.ends_at);
      if (subscription.status === 'cancelled' && subscription.ends_at === null) throw new ApiError(403, 'subscription_inactive');
      if (subscription.status === 'on_trial' && subscription.ends_at === null) throw new ApiError(403, 'subscription_inactive');
    }
    // A plan downgrade preserves the oldest active device slots. Existing cached
    // leases remain bounded by their original expiry; new leases use the new cap.
    const slot = await env.DB.prepare(`SELECT count(*) AS position FROM installations i
      JOIN installations current ON current.id=? AND current.license_id=?
      WHERE i.license_id=? AND i.revoked=0 AND
      (i.created_at<current.created_at OR (i.created_at=current.created_at AND i.id<=current.id))`)
      .bind(device.id, license.id, license.id).first<{position: number}>();
    if (slot && slot.position > deviceLimit) throw new ApiError(409, 'device_limit');
  }
  if (expires <= time || device.revoked) throw new ApiError(403, 'access_expired');
  if (!env.ISSUER.startsWith('https://') || env.ISSUER.includes('.invalid')) throw new ApiError(503, 'issuer_not_configured');
  const key = await importPKCS8(env.ENTITLEMENT_PRIVATE_KEY, 'EdDSA');
  return new SignJWT({ device: device.credential_hash, plan, mode, major, features, schema: 1 })
    .setProtectedHeader({ alg: 'EdDSA', kid: env.SIGNING_KEY_ID, typ: 'JWT' })
    .setIssuer(env.ISSUER).setAudience('looksy-desktop').setSubject(device.customer_id)
    .setIssuedAt(time).setExpirationTime(expires).sign(key);
}
