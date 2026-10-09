import { ApiError, body, hash, json, now, str, type ServiceEnv } from './core';

type Actor = { subject: string; role: string };

async function audit(env: ServiceEnv, actor: Actor, action: string, target: string, reason: string): Promise<void> {
  await env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)')
    .bind(crypto.randomUUID(), actor.subject, action, target, reason, now()).run();
}

export async function operationsSummary(env: ServiceEnv): Promise<Response> {
  const time = now();
  const [webhooks, stale] = await Promise.all([
    env.DB.prepare(`SELECT count(*) AS pending,
      sum(CASE WHEN attempts>0 THEN 1 ELSE 0 END) AS failed,
      sum(CASE WHEN next_retry_at<=? THEN 1 ELSE 0 END) AS due,
      min(received_at) AS oldestPendingAt,
      max(attempts) AS maxAttempts
      FROM webhook_receipts WHERE processed_at IS NULL`).bind(time).first<Record<string, number | null>>(),
    env.DB.prepare('SELECT count(*) AS stale FROM licenses WHERE checked_at<? AND blocked=0').bind(time - 86400).first<{stale: number}>(),
  ]);
  return json({ generatedAt: time, webhooks: {
    pending: webhooks?.pending ?? 0, failed: webhooks?.failed ?? 0, due: webhooks?.due ?? 0,
    oldestPendingAt: webhooks?.oldestPendingAt ?? null, maxAttempts: webhooks?.maxAttempts ?? 0,
  }, licenses: { stale: stale?.stale ?? 0 }, backup: { lastSuccessfulAt: null } });
}

export async function customerExport(request: Request, env: ServiceEnv, actor: Actor): Promise<Response> {
  const input = await body(request);
  const customerId = str(input.customerId, 200), reason = str(input.reason, 500);
  const customer = await env.DB.prepare('SELECT id,email,created_at FROM customers WHERE id=?').bind(customerId).first();
  if (!customer) throw new ApiError(404, 'not_found');
  const [licenses, devices, auditRows] = await Promise.all([
    env.DB.prepare(`SELECT id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,
      expires_at,checked_at,blocked FROM licenses WHERE customer_id=? LIMIT 1001`).bind(customerId).all(),
    env.DB.prepare(`SELECT id,license_id,trial_ends_at,revoked,created_at,last_seen_at,app_version,platform
      FROM installations WHERE customer_id=? LIMIT 1001`).bind(customerId).all(),
    env.DB.prepare('SELECT actor,action,target,reason,created_at FROM audit WHERE target=? ORDER BY created_at LIMIT 1001').bind(customerId).all(),
  ]);
  if (licenses.results.length > 1000 || devices.results.length > 1000 || auditRows.results.length > 1000) {
    throw new ApiError(413, 'export_too_large');
  }
  await audit(env, actor, 'customer_export', customerId, reason);
  return json({ export: { schema: 1, generatedAt: now(), customer, licenses: licenses.results,
    devices: devices.results, audit: auditRows.results } });
}

export async function deleteCustomer(env: ServiceEnv, actor: Actor, customerIdValue: unknown, reasonValue: unknown): Promise<Response> {
  if (actor.role !== 'admin') throw new ApiError(403, 'admin_required');
  const customerId = str(customerIdValue, 200), reason = str(reasonValue, 500);
  const customer = await env.DB.prepare('SELECT provider_customer_id FROM customers WHERE id=?').bind(customerId)
    .first<{provider_customer_id: string | null}>();
  if (!customer) throw new ApiError(404, 'not_found');
  const providerHash = customer.provider_customer_id ? await hash(customer.provider_customer_id) : null;
  const time = now();
  await env.DB.batch([
    env.DB.prepare(`INSERT INTO privacy_tombstones(customer_id,provider_customer_hash,deleted_at) VALUES(?,?,?)
      ON CONFLICT(customer_id) DO UPDATE SET provider_customer_hash=excluded.provider_customer_hash,deleted_at=excluded.deleted_at`)
      .bind(customerId, providerHash, time),
    env.DB.prepare('UPDATE customers SET email=NULL,provider_customer_id=NULL WHERE id=?').bind(customerId),
    env.DB.prepare("UPDATE licenses SET blocked=1,status='deleted' WHERE customer_id=?").bind(customerId),
    env.DB.prepare(`UPDATE installations SET revoked=1,app_version='',platform='',last_seen_at=?
      WHERE customer_id=?`).bind(time, customerId),
    env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)').bind(crypto.randomUUID(), actor.subject,
      'customer_personal_data_deleted', customerId, reason, time),
  ]);
  return json({ ok: true, deleted: true });
}

export async function retryWebhook(env: ServiceEnv, actor: Actor, digestValue: unknown, reasonValue: unknown): Promise<Response> {
  if (actor.role !== 'admin') throw new ApiError(403, 'admin_required');
  const digest = str(digestValue, 64), reason = str(reasonValue, 500);
  if (!/^[0-9a-f]{64}$/.test(digest)) throw new ApiError(400, 'invalid_webhook_id');
  const result = await env.DB.batch([
    env.DB.prepare(`UPDATE webhook_receipts SET attempts=0,next_retry_at=?,last_error_code=NULL
      WHERE digest=? AND processed_at IS NULL`).bind(now(), digest),
    env.DB.prepare(`INSERT INTO audit SELECT ?,?,?,?,?,? WHERE EXISTS
      (SELECT 1 FROM webhook_receipts WHERE digest=? AND processed_at IS NULL)`).bind(
        crypto.randomUUID(), actor.subject, 'webhook_retry_requested', `webhook:${digest}`, reason, now(), digest),
  ]);
  if (!result[0].meta.changes) throw new ApiError(404, 'retryable_webhook_not_found');
  return json({ ok: true, retryScheduled: true });
}

export async function adminOperations(request: Request, env: ServiceEnv, actor: Actor, path: string): Promise<Response | null> {
  if (request.method === 'GET' && path === '/admin/api/operations') {
    return operationsSummary(env);
  }
  if (request.method === 'GET' && path === '/admin/api/webhooks') {
    if (actor.role !== 'admin') throw new ApiError(403, 'admin_required');
    const rows = await env.DB.prepare(`SELECT digest,event,received_at,attempts,next_retry_at,last_error_code
      FROM webhook_receipts WHERE processed_at IS NULL ORDER BY received_at LIMIT 50`).all();
    return json({ webhooks: rows.results });
  }
  if (request.method !== 'POST' || request.headers.get('Origin') !== new URL(request.url).origin ||
      request.headers.get('X-Looksy-Admin') !== '1') return null;
  if (path === '/admin/api/customer-export') return customerExport(request, env, actor);
  if (path === '/admin/api/customer-delete') {
    const input = await body(request);
    return deleteCustomer(env, actor, input.customerId, input.reason);
  }
  if (path === '/admin/api/webhook-retry') {
    const input = await body(request);
    return retryWebhook(env, actor, input.digest, input.reason);
  }
  return null;
}
