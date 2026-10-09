import { createRemoteJWKSet, jwtVerify } from 'jose';
import { ApiError, authenticate, body, boundedInt, boundedText, DAY, hash, hmacKey, id, issueLease, json, now, object, rateLimit, str, type Installation, type ServiceEnv } from './core';
import { processEvent, syncLicense, validatePurchase } from './billing';
import { adminOperations } from './operations';

async function startTrial(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request, env, 'trial', 5);
  const input = await body(request);
  const credential = str(input.credential, 43);
  if (!/^[A-Za-z0-9_-]{43}$/.test(credential)) throw new ApiError(400, 'invalid_credential');
  const device = await registerInstallation(env, credential);
  return json({ lease: await issueLease(env, device), installationId: device.id });
}

async function registerInstallation(env: ServiceEnv, credential: string): Promise<Installation> {
  const digest = await hash(credential), time = now();
  const customerId = `guest:${digest}`, installationId = crypto.randomUUID();
  await env.DB.batch([
    env.DB.prepare('INSERT OR IGNORE INTO customers(id,created_at) VALUES(?,?)').bind(customerId, time),
    env.DB.prepare(`INSERT OR IGNORE INTO installations(id,credential_hash,customer_id,trial_ends_at,created_at,last_seen_at)
      VALUES(?,?,?,?,?,?)`).bind(installationId, digest, customerId, time + boundedInt(env.TRIAL_DAYS, 1, 60) * DAY, time, time),
  ]);
  const device = await env.DB.prepare('SELECT * FROM installations WHERE credential_hash=?').bind(digest).first<Installation>();
  if (!device) throw new ApiError(503, 'storage_unavailable');
  return device;
}

async function staffIdentity(request: Request, env: ServiceEnv): Promise<{subject: string; role: string; service: boolean}> {
  if (!/^[a-z0-9-]+\.cloudflareaccess\.com$/.test(env.ACCESS_TEAM_DOMAIN) || !env.ACCESS_AUDIENCE) throw new ApiError(503, 'staff_auth_not_configured');
  const token = request.headers.get('Cf-Access-Jwt-Assertion');
  if (!token) throw new ApiError(401, 'staff_auth_required');
  const issuer = `https://${env.ACCESS_TEAM_DOMAIN}`;
  let subject: string, service: boolean;
  try {
    const { payload } = await jwtVerify(token, createRemoteJWKSet(new URL(`${issuer}/cdn-cgi/access/certs`)), {
      issuer, audience: env.ACCESS_AUDIENCE, algorithms: ['RS256'], requiredClaims: ['sub', 'exp', 'iat'],
    });
    if (payload.sub === '') {
      const commonName = payload.common_name;
      if (payload.type !== 'app' || typeof commonName !== 'string' || !/^[A-Za-z0-9._-]{8,200}$/.test(commonName)) {
        throw new Error('invalid service identity');
      }
      subject = `service:${commonName}`;
      service = true;
    } else {
      subject = str(payload.sub);
      service = false;
    }
  } catch { throw new ApiError(401, 'invalid_staff_identity'); }
  const staff = await env.DB.prepare('SELECT role FROM staff WHERE subject=?').bind(subject).first<{role: string}>();
  if (!staff) throw new ApiError(403, 'staff_role_required');
  if (service && staff.role !== 'support') throw new ApiError(403, 'service_support_role_required');
  return { subject, role: staff.role, service };
}

async function admin(request: Request, env: ServiceEnv, path: string): Promise<Response> {
  const actor = await staffIdentity(request, env);
  if (actor.service && path !== '/admin/api/operations') throw new ApiError(403, 'service_read_scope_required');
  const operational = await adminOperations(request, env, actor, path);
  if (operational) return operational;
  if (request.method === 'GET' && path === '/admin/api/customers') {
    const url = new URL(request.url), query = (url.searchParams.get('q') || '').slice(0, 320);
    const after = (url.searchParams.get('after') || '').slice(0, 100);
    const rows = await env.DB.prepare(`SELECT c.id,c.email,c.created_at,
      (SELECT max(last_seen_at) FROM installations WHERE customer_id=c.id) AS last_seen_at
      FROM customers c WHERE c.id>? AND (c.id=? OR c.email=? OR ?='') ORDER BY c.id LIMIT 51`)
      .bind(after, query, query, query).all();
    return json({ customers: rows.results.slice(0, 50), next: rows.results.length > 50 ? rows.results[49].id : null, role: actor.role });
  }
  if (request.method === 'GET' && path === '/admin/api/customer') {
    const customer = str(new URL(request.url).searchParams.get('id'));
    const [licenses, devices, audit] = await Promise.all([
      env.DB.prepare('SELECT * FROM licenses WHERE customer_id=? LIMIT 100').bind(customer).all(),
      env.DB.prepare('SELECT id,license_id,trial_ends_at,revoked,last_seen_at,app_version,platform FROM installations WHERE customer_id=? LIMIT 100').bind(customer).all(),
      env.DB.prepare('SELECT actor,action,target,reason,created_at FROM audit WHERE target=? ORDER BY created_at DESC LIMIT 100').bind(customer).all(),
    ]);
    return json({ licenses: licenses.results, devices: devices.results, audit: audit.results });
  }
  if (request.method === 'POST' && path === '/admin/api/action') {
    if (request.headers.get('Origin') !== new URL(request.url).origin || request.headers.get('X-Looksy-Admin') !== '1') throw new ApiError(403, 'invalid_origin');
    const input = await body(request), action = str(input.action), target = str(input.target), reason = str(input.reason, 500);
    let statement: D1PreparedStatement;
    let customer: string | undefined;
    if (action === 'revoke_device' || action === 'extend_trial') {
      const device = await env.DB.prepare('SELECT * FROM installations WHERE id=?').bind(target).first<Installation>();
      if (!device) throw new ApiError(404, 'not_found');
      customer = device.customer_id;
      if (action === 'extend_trial') {
        if (device.license_id || device.revoked) throw new ApiError(409, 'not_a_trial');
        const days = Number(input.days);
        if (!Number.isInteger(days) || days < 1 || days > (actor.role === 'admin' ? 60 : 14)) throw new ApiError(400, 'invalid_trial_extension');
        statement = env.DB.prepare('UPDATE installations SET trial_ends_at=? WHERE id=?')
          .bind(Math.max(now(), device.trial_ends_at) + days * DAY, target);
      } else statement = env.DB.prepare('UPDATE installations SET revoked=1 WHERE id=?').bind(target);
    } else if (action === 'block_license' || action === 'unblock_license') {
      if (actor.role !== 'admin') throw new ApiError(403, 'admin_required');
      const license = await env.DB.prepare('SELECT customer_id FROM licenses WHERE id=?').bind(id(target)).first<{customer_id: string}>();
      if (!license) throw new ApiError(404, 'not_found');
      customer = license.customer_id;
      statement = env.DB.prepare('UPDATE licenses SET blocked=? WHERE id=?').bind(action === 'block_license' ? 1 : 0, target);
    } else throw new ApiError(400, 'unknown_action');
    await env.DB.batch([statement, env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)')
      .bind(crypto.randomUUID(), actor.subject, `${action}:${target}`, customer, reason, now())]);
    return json({ ok: true });
  }
  if (request.method === 'GET' && !path.startsWith('/admin/api/')) return env.ASSETS.fetch(request);
  throw new ApiError(404, 'not_found');
}

async function webhook(request: Request, env: ServiceEnv): Promise<Response> {
  const raw = await boundedText(request, 131072), signature = request.headers.get('X-Signature') || '';
  if (!/^[0-9a-f]{64}$/i.test(signature)) throw new ApiError(401, 'invalid_signature');
  const bytes = Uint8Array.from(signature.match(/../g)!, value => parseInt(value, 16));
  if (!await crypto.subtle.verify('HMAC', await hmacKey(env.LEMON_WEBHOOK_SECRET), bytes, new TextEncoder().encode(raw))) throw new ApiError(401, 'invalid_signature');
  let payload: Record<string, unknown>;
  try { payload = object(JSON.parse(raw)); } catch { throw new ApiError(400, 'invalid_json'); }
  const meta = object(payload.meta), data = object(payload.data);
  const event = str(meta.event_name), resourceId = id(data.id), digest = await hash(raw);
  if (!/^(order_(created|refunded)|license_key_(created|updated)|subscription_(created|updated|cancelled|resumed|expired|paused|unpaused|payment_(failed|success|recovered)))$/.test(event)) return json({ ignored: true });
  if (meta.test_mode !== (env.ALLOW_TEST_PURCHASES === 'true')) throw new ApiError(403, 'wrong_billing_environment');
  await env.DB.prepare('INSERT OR IGNORE INTO webhook_receipts(digest,event,resource_id,received_at) VALUES(?,?,?,?)')
    .bind(digest, event, resourceId, now()).run();
  const receipt = await env.DB.prepare('SELECT processed_at,attempts,next_retry_at FROM webhook_receipts WHERE digest=?')
    .bind(digest).first<{processed_at: number | null; attempts: number; next_retry_at: number}>();
  if (receipt?.processed_at) return json({ duplicate: true });
  if ((receipt?.attempts ?? 0) >= 8) return json({ pendingReview: true }, 202);
  if ((receipt?.attempts ?? 0) > 0 && (receipt?.next_retry_at ?? 0) > now()) return json({ retryScheduled: true }, 202);
  // Fetch current provider state, never apply stale event contents to permissions.
  try {
    await processEvent(env, event, resourceId);
    await env.DB.prepare('UPDATE webhook_receipts SET processed_at=?,last_error_code=NULL WHERE digest=?').bind(now(), digest).run();
  } catch (error) {
    await markWebhookFailure(env, digest);
    throw error;
  }
  return json({ ok: true });
}

async function markWebhookFailure(env: ServiceEnv, digest: string): Promise<void> {
  const time = now();
  await env.DB.prepare(`UPDATE webhook_receipts SET attempts=attempts+1,
    next_retry_at=?+min(86400,60*(1 << min(attempts,10))),last_error_code='processing_failed'
    WHERE digest=? AND processed_at IS NULL`).bind(time, digest).run();
}

async function recover(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request, env, 'recovery', 10);
  const input = await body(request), license = await validatePurchase(env, str(input.licenseKey, 128));
  const credential = str(input.credential, 43);
  if (!/^[A-Za-z0-9_-]{43}$/.test(credential)) throw new ApiError(400, 'invalid_credential');
  if (!['perpetual', 'subscription'].includes(license.mode)) throw new ApiError(403, 'paid_license_required');
  const digest = await hash(credential);
  const existing = await env.DB.prepare('SELECT * FROM installations WHERE credential_hash=?').bind(digest).first<Installation>();
  if (existing) {
    if (existing.customer_id !== license.customer_id || existing.license_id !== license.id || existing.revoked) {
      throw new ApiError(409, 'credential_already_used');
    }
    return json({ lease: await issueLease(env, existing), installationId: existing.id });
  }
  const previous = input.previousCredential === undefined ? null : str(input.previousCredential, 43);
  let previousDevice: Installation | null = null;
  if (previous !== null) {
    if (!/^[A-Za-z0-9_-]{43}$/.test(previous)) throw new ApiError(400, 'invalid_previous_credential');
    if (previous === credential) throw new ApiError(400, 'credential_must_rotate');
    previousDevice = await env.DB.prepare('SELECT * FROM installations WHERE credential_hash=?').bind(await hash(previous)).first<Installation>();
    const paidMatch = previousDevice?.license_id === license.id && previousDevice.customer_id === license.customer_id;
    const guestMatch = previousDevice?.license_id === null && previousDevice.customer_id.startsWith('guest:');
    if (!previousDevice || (!paidMatch && !guestMatch)) {
      throw new ApiError(403, 'previous_device_not_eligible');
    }
  }
  const time = now(), installationId = crypto.randomUUID();
  const device: Installation = {
    id: installationId, credential_hash: digest, customer_id: license.customer_id,
    license_id: license.id, trial_ends_at: time, revoked: 0,
  };
  // Verify live paid entitlement before consuming a seat. The INSERT trigger below
  // applies the device cap in the same database transaction as old-device revoke.
  await issueLease(env, device);
  const statements: D1PreparedStatement[] = [];
  if (previousDevice && !previousDevice.revoked) {
    if (previousDevice.license_id === null) {
      statements.push(env.DB.prepare(`UPDATE installations SET revoked=1 WHERE id=? AND revoked=0 AND license_id IS NULL
        AND EXISTS(SELECT 1 FROM licenses WHERE id=? AND customer_id=? AND blocked=0 AND status IN ('active','inactive'))`)
        .bind(previousDevice.id, license.id, license.customer_id));
    } else {
      statements.push(env.DB.prepare(`UPDATE installations SET revoked=1 WHERE id=? AND revoked=0 AND license_id=?
        AND EXISTS(SELECT 1 FROM licenses WHERE id=? AND customer_id=? AND blocked=0 AND status IN ('active','inactive'))`)
        .bind(previousDevice.id, license.id, license.id, license.customer_id));
    }
  }
  statements.push(env.DB.prepare(`INSERT INTO installations(id,credential_hash,customer_id,license_id,trial_ends_at,created_at,last_seen_at)
    SELECT ?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM licenses WHERE id=? AND customer_id=? AND blocked=0
      AND status IN ('active','inactive'))`).bind(installationId, digest, license.customer_id, license.id,
      time, time, time, license.id, license.customer_id));
  try { await env.DB.batch(statements); }
  catch (error) {
    if (String(error).includes('device_limit')) throw new ApiError(409, 'device_limit');
    if (String(error).includes('license_inactive')) throw new ApiError(409, 'license_no_longer_active');
    throw error;
  }
  const inserted = await env.DB.prepare('SELECT * FROM installations WHERE id=?').bind(installationId).first<Installation>();
  if (!inserted) throw new ApiError(409, 'license_no_longer_active');
  try { return json({ lease: await issueLease(env, inserted), installationId }); }
  catch (error) {
    await env.DB.prepare('UPDATE installations SET revoked=1 WHERE id=?').bind(installationId).run();
    throw error;
  }
}

async function route(request: Request, env: ServiceEnv): Promise<Response> {
  const url = new URL(request.url), path = url.pathname;
  if (path.startsWith('/admin')) return admin(request, env, path);
  if (request.method === 'GET' && path === '/health') return json({ service: 'looksy-customers', version: 1 });
  if (request.method === 'POST' && path === '/v1/webhooks/lemon') return webhook(request, env);
  if (request.method === 'POST' && path === '/v1/trial') return startTrial(request, env);
  if (request.method === 'POST' && path === '/v1/recover') return recover(request, env);
  await rateLimit(request, env, 'account', 30);
  if (request.method === 'POST' && path === '/v1/activate') {
    const token = request.headers.get('Authorization')?.replace(/^Bearer /, '');
    if (!token || !/^[A-Za-z0-9_-]{43}$/.test(token)) throw new ApiError(401, 'authentication_required');
    const input = await body(request);
    const license = await validatePurchase(env, str(input.licenseKey, 128));
    const device = await registerInstallation(env, token);
    // Verify access before consuming a device slot (e.g. refunded or missing subscription).
    await issueLease(env, { ...device, customer_id: license.customer_id, license_id: license.id });
    try {
      await env.DB.prepare('UPDATE installations SET customer_id=?,license_id=? WHERE id=? AND revoked=0')
        .bind(license.customer_id, license.id, device.id).run();
    } catch (error) {
      if (String(error).includes('device_limit')) throw new ApiError(409, 'device_limit');
      throw error;
    }
    // Re-read after admission: another request may have revoked the installation.
    return json({ lease: await issueLease(env, await authenticate(request, env)) });
  }
  const device = await authenticate(request, env);
  if (request.method === 'POST' && path === '/v1/refresh') {
    const input = await body(request);
    if (device.license_id) {
      try { await syncLicense(env, device.license_id); }
      catch (error) {
        // A provider outage can use the remaining original grace, never renew it.
        if (!(error instanceof ApiError) || error.status !== 502) throw error;
      }
    }
    const version = str(input.version, 40), platform = str(input.platform, 40);
    if (!/^[0-9A-Za-z.+_-]+$/.test(version) || !['mac-arm', 'mac-intel', 'windows', 'linux'].includes(platform)) throw new ApiError(400, 'invalid_client_metadata');
    await env.DB.prepare('UPDATE installations SET last_seen_at=?,app_version=?,platform=? WHERE id=?').bind(now(), version, platform, device.id).run();
    return json({ lease: await issueLease(env, device) });
  }
  if (request.method === 'GET' && path === '/v1/devices') {
    const rows = await env.DB.prepare('SELECT id,app_version,platform,last_seen_at,revoked FROM installations WHERE customer_id=? ORDER BY last_seen_at DESC LIMIT 100')
      .bind(device.customer_id).all();
    return json({ devices: rows.results, current: device.id });
  }
  if (request.method === 'POST' && path === '/v1/devices/revoke') {
    const input = await body(request), target = str(input.id);
    const result = await env.DB.prepare('UPDATE installations SET revoked=1 WHERE id=? AND customer_id=?').bind(target, device.customer_id).run();
    if (!result.meta.changes) throw new ApiError(404, 'not_found');
    return json({ ok: true });
  }
  throw new ApiError(404, 'not_found');
}

export default {
  async fetch(request: Request, env: ServiceEnv): Promise<Response> {
    try { return await route(request, env); }
    catch (error) {
      if (error instanceof ApiError) return json({ error: error.code }, error.status);
      // Never log exceptions containing provider bodies, credentials, or request URLs.
      const incident = crypto.randomUUID();
      console.error(JSON.stringify({ event: 'customer_service_failure', incident }));
      return json({ error: 'service_unavailable', incident }, 503);
    }
  },
  async scheduled(_controller: ScheduledController, env: ServiceEnv): Promise<void> {
    const time = now();
    const pending = await env.DB.prepare(`SELECT digest,event,resource_id FROM webhook_receipts
      WHERE processed_at IS NULL AND attempts<8 AND next_retry_at<=? ORDER BY next_retry_at,received_at LIMIT 20`).bind(time)
      .all<{digest: string; event: string; resource_id: string}>();
    for (const item of pending.results) {
      try {
        await processEvent(env, item.event, item.resource_id);
        await env.DB.prepare('UPDATE webhook_receipts SET processed_at=?,last_error_code=NULL WHERE digest=?').bind(now(), item.digest).run();
      } catch { await markWebhookFailure(env, item.digest); console.error(JSON.stringify({ event: 'billing_retry_failed', receipt: item.digest })); }
    }
    const licenses = await env.DB.prepare('SELECT id FROM licenses WHERE checked_at<? ORDER BY checked_at LIMIT 20').bind(time - DAY).all<{id: string}>();
    for (const license of licenses.results) {
      try { await syncLicense(env, license.id); }
      catch { console.error(JSON.stringify({ event: 'billing_reconcile_failed', licenseId: license.id })); }
    }
    await env.DB.batch([
      env.DB.prepare('DELETE FROM rate_buckets WHERE expires_at<?').bind(time),
      env.DB.prepare('DELETE FROM webhook_receipts WHERE processed_at IS NOT NULL AND received_at<?').bind(time - 90 * DAY),
    ]);
  },
} satisfies ExportedHandler<ServiceEnv>;
