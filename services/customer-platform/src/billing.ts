import { ApiError, boundedText, id, now, object, productPolicy, str, timestamp, type License, type ServiceEnv } from './core';

const API = 'https://api.lemonsqueezy.com/v1/';
async function refreshRevision(env: ServiceEnv, key: string): Promise<number> {
  const row = await env.DB.prepare(`INSERT INTO refresh_sequences(id,revision) VALUES(?,1)
    ON CONFLICT(id) DO UPDATE SET revision=revision+1 RETURNING revision`).bind(key).first<{revision: number}>();
  if (!row) throw new ApiError(503, 'storage_unavailable');
  return row.revision;
}
async function provider(env: ServiceEnv, path: string, form?: URLSearchParams): Promise<Record<string, unknown>> {
  if (!env.LEMON_API_KEY || !env.LEMON_STORE_ID) throw new ApiError(503, 'billing_not_configured');
  let response: Response;
  try { response = await fetch(API + path, {
    method: form ? 'POST' : 'GET', redirect: 'error', signal: AbortSignal.timeout(10000),
    headers: form ? { Accept: 'application/json' } : { Accept: 'application/vnd.api+json', Authorization: `Bearer ${env.LEMON_API_KEY}` },
    body: form,
  }); } catch { throw new ApiError(502, 'billing_unavailable'); }
  if (!response.ok) { await response.body?.cancel(); throw new ApiError(502, 'billing_unavailable'); }
  return object(JSON.parse(await boundedText(response, 262144)));
}
async function resource(env: ServiceEnv, kind: string, resourceId: string): Promise<Record<string, unknown>> {
  const data = object((await provider(env, `${kind}/${id(resourceId)}`)).data);
  if (id(data.id) !== resourceId || data.type !== kind) throw new ApiError(502, 'invalid_provider_response');
  return object(data.attributes);
}
function verifyStore(env: ServiceEnv, attributes: Record<string, unknown>): void {
  if (id(attributes.store_id) !== env.LEMON_STORE_ID) throw new ApiError(403, 'wrong_store');
}
function verifyMode(env: ServiceEnv, attributes: Record<string, unknown>): void {
  if (typeof attributes.test_mode !== 'boolean' || attributes.test_mode !== (env.ALLOW_TEST_PURCHASES === 'true')) throw new ApiError(403, 'wrong_billing_environment');
}

export async function syncSubscription(env: ServiceEnv, subscriptionId: string): Promise<void> {
  const checked = now();
  const revision = await refreshRevision(env, `subscription:${subscriptionId}`);
  const a = await resource(env, 'subscriptions', subscriptionId);
  verifyStore(env, a); verifyMode(env, a);
  const variant = id(a.variant_id);
  const policy = productPolicy(env, variant);
  if (policy.mode !== 'subscription' || policy.productId !== id(a.product_id)) throw new ApiError(403, 'wrong_product');
  const status = str(a.status, 30);
  const ends = status === 'on_trial' ? timestamp(a.trial_ends_at) : timestamp(a.ends_at);
  await env.DB.prepare(`INSERT INTO subscriptions(id,order_item_id,variant_id,status,ends_at,checked_at,refresh_revision) VALUES(?,?,?,?,?,?,?)
    ON CONFLICT(id) DO UPDATE SET variant_id=excluded.variant_id,status=excluded.status,ends_at=excluded.ends_at,
    checked_at=excluded.checked_at,refresh_revision=excluded.refresh_revision
    WHERE subscriptions.refresh_revision<excluded.refresh_revision`)
    .bind(subscriptionId, id(a.order_item_id), variant, status, ends, checked, revision).run();
  await env.DB.prepare(`UPDATE licenses SET device_limit=? WHERE order_item_id=? AND mode='subscription'
    AND EXISTS(SELECT 1 FROM subscriptions WHERE id=? AND refresh_revision=?)`)
    .bind(policy.deviceLimit, id(a.order_item_id), subscriptionId, revision).run();
}

export async function syncLicense(env: ServiceEnv, licenseId: string): Promise<License> {
  const checked = now();
  const revision = await refreshRevision(env, `license:${licenseId}`);
  const a = await resource(env, 'license-keys', licenseId);
  verifyStore(env, a);
  const orderId = id(a.order_id), itemId = id(a.order_item_id);
  const order = await resource(env, 'orders', orderId);
  verifyStore(env, order); verifyMode(env, order);
  if (id(order.customer_id) !== id(a.customer_id)) throw new ApiError(502, 'invalid_provider_response');
  const item = await resource(env, 'order-items', itemId);
  if (id(item.order_id) !== orderId) throw new ApiError(502, 'invalid_provider_response');
  const variant = id(item.variant_id), policy = productPolicy(env, variant);
  if (id(a.product_id) !== policy.productId || id(item.product_id) !== policy.productId) throw new ApiError(403, 'wrong_product');
  let status = str(a.status, 30);
  if (a.disabled === true || a.disabled === 1 || order.status !== 'paid' || order.refunded === true) status = 'disabled';
  const providerCustomer = id(a.customer_id), customerId = `lemon:${env.LEMON_STORE_ID}:${providerCustomer}`;
  const license: License = {
    id: licenseId, customer_id: customerId, order_id: orderId, order_item_id: itemId, variant_id: variant,
    plan: policy.plan, mode: policy.mode, major: policy.major, features: JSON.stringify(policy.features),
    device_limit: policy.deviceLimit, status, expires_at: timestamp(a.expires_at), checked_at: checked, blocked: 0,
  };
  // The statement intentionally preserves staff revocation across billing updates.
  await env.DB.batch([
    env.DB.prepare(`INSERT INTO customers(id,provider_customer_id,email,created_at) VALUES(?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET
        provider_customer_id=CASE WHEN EXISTS(SELECT 1 FROM privacy_tombstones WHERE customer_id=excluded.id)
          THEN NULL ELSE excluded.provider_customer_id END,
        email=CASE WHEN EXISTS(SELECT 1 FROM privacy_tombstones WHERE customer_id=excluded.id)
          THEN NULL ELSE excluded.email END`).bind(customerId, providerCustomer, str(a.user_email, 320), checked),
    env.DB.prepare(`INSERT INTO licenses(id,customer_id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,expires_at,checked_at,refresh_revision)
      VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET plan=excluded.plan,features=excluded.features,
      device_limit=excluded.device_limit,status=CASE WHEN EXISTS(SELECT 1 FROM privacy_tombstones WHERE customer_id=excluded.customer_id)
        THEN 'deleted' ELSE excluded.status END,expires_at=excluded.expires_at,checked_at=excluded.checked_at
      ,refresh_revision=excluded.refresh_revision WHERE licenses.refresh_revision<excluded.refresh_revision`)
      .bind(license.id, customerId, orderId, itemId, variant,
        license.plan, license.mode, license.major, license.features, license.device_limit, status, license.expires_at, checked, revision),
  ]);
  if (policy.mode === 'subscription') {
    let subscription = await env.DB.prepare('SELECT id FROM subscriptions WHERE order_item_id=?').bind(itemId).first<{id: string}>();
    if (!subscription) {
      // Recover a delayed/missed initial webhook using an exact provider filter.
      const query = new URLSearchParams({'filter[store_id]': env.LEMON_STORE_ID,
        'filter[order_item_id]': itemId, 'page[size]': '2'});
      const listing = await provider(env, `subscriptions?${query}`);
      if (!Array.isArray(listing.data) || listing.data.length > 1) throw new ApiError(502, 'invalid_provider_response');
      if (listing.data.length === 1) {
        const candidate = object(listing.data[0]), attributes = object(candidate.attributes);
        if (candidate.type !== 'subscriptions' || id(attributes.order_item_id) !== itemId ||
            id(attributes.customer_id) !== providerCustomer) throw new ApiError(502, 'invalid_provider_response');
        subscription = {id: id(candidate.id)};
      }
    }
    if (subscription) await syncSubscription(env, subscription.id);
  }
  const stored = await env.DB.prepare('SELECT * FROM licenses WHERE id=?').bind(licenseId).first<License>();
  if (!stored) throw new ApiError(503, 'storage_unavailable');
  return stored;
}

export async function validatePurchase(env: ServiceEnv, key: string): Promise<License> {
  const result = await provider(env, 'licenses/validate', new URLSearchParams({ license_key: key }));
  if (result.valid !== true) throw new ApiError(403, 'invalid_license');
  const meta = object(result.meta);
  verifyStore(env, meta);
  const policy = productPolicy(env, id(meta.variant_id));
  if (id(meta.product_id) !== policy.productId) throw new ApiError(403, 'wrong_product');
  return syncLicense(env, id(object(result.license_key).id));
}

export async function processEvent(env: ServiceEnv, event: string, resourceId: string): Promise<void> {
  if (event.startsWith('license_key_')) { await syncLicense(env, resourceId); return; }
  if (event.startsWith('subscription_') && !event.startsWith('subscription_payment_')) {
    await syncSubscription(env, resourceId); return;
  }
  let orderId = resourceId;
  if (event.startsWith('subscription_payment_')) {
    const invoice = await resource(env, 'subscription-invoices', resourceId);
    await syncSubscription(env, id(invoice.subscription_id)); return;
  }
  if (!event.startsWith('order_')) return;
  orderId = id(orderId);
  const licenses = await env.DB.prepare('SELECT id FROM licenses WHERE order_id=? LIMIT 100').bind(orderId).all<{id: string}>();
  for (const license of licenses.results) await syncLicense(env, license.id);
}
