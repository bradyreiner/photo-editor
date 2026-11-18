import { requireAccount } from './accounts';
import { ApiError, authenticate, body, boundedText, hash, hmacKey, json, now, object, providerProductPolicy, rateLimit, str, timestamp, type License, type ProductPolicy, type ServiceEnv } from './core';

type Mapping = { license_id: string; account_id: string; customer_id: string; transaction_id: string; subscription_id: string | null; environment: string };
type Checkout = { transaction_id: string; account_id: string; customer_id: string; price_id: string; environment: string };
function paddleId(value: unknown, prefix: string): string {
  const result = str(value, 40);
  if (!new RegExp(`^${prefix}_[a-z0-9]{26}$`).test(result)) throw new ApiError(502, 'invalid_paddle_response');
  return result;
}
function environment(env: ServiceEnv): 'sandbox' | 'live' {
  if (!['sandbox','live'].includes(env.PADDLE_ENVIRONMENT)) throw new ApiError(503,'paddle_not_configured');
  // A live customer service must never accept sandbox fulfillment (or vice versa).
  if ((env.PADDLE_ENVIRONMENT==='sandbox') !== (env.ALLOW_TEST_PURCHASES==='true')) throw new ApiError(503,'wrong_billing_environment');
  return env.PADDLE_ENVIRONMENT as 'sandbox' | 'live';
}
async function api(env: ServiceEnv, path: string, input?: Record<string,unknown>): Promise<Record<string,unknown>> {
  const mode = environment(env);
  if (!env.PADDLE_API_KEY) throw new ApiError(503,'paddle_not_configured');
  let response: Response;
  try { response=await fetch(`https://${mode==='sandbox'?'sandbox-api':'api'}.paddle.com/${path}`, {
    method:input?'POST':'GET', redirect:'error', signal:AbortSignal.timeout(10000),
    headers:{Authorization:`Bearer ${env.PADDLE_API_KEY}`,'Content-Type':'application/json','Paddle-Version':'1'},
    body:input?JSON.stringify(input):undefined,
  }); } catch { throw new ApiError(502,'billing_unavailable'); }
  if (!response.ok) { await response.body?.cancel(); throw new ApiError(502,'billing_unavailable'); }
  try { return object(JSON.parse(await boundedText(response,524288))); }
  catch(error) { if(error instanceof ApiError) throw error; throw new ApiError(502,'invalid_paddle_response'); }
}
async function resource(env: ServiceEnv, kind: string, value: string, prefix: string): Promise<Record<string,unknown>> {
  const result=object((await api(env,`${kind}/${paddleId(value,prefix)}`)).data);
  if (result.id!==value) throw new ApiError(502,'invalid_paddle_response');
  return result;
}
function items(env: ServiceEnv, value: unknown): {priceId:string;policy:ProductPolicy} {
  // Looksy initially sells one user/offer per checkout, without mixed carts/add-ons.
  if(!Array.isArray(value) || value.length!==1) throw new ApiError(403,'unsupported_paddle_items');
  const item=object(value[0]), price=object(item.price), priceId=paddleId(price.id,'pri');
  if(item.quantity!==1) throw new ApiError(403,'unsupported_paddle_quantity');
  const policy=providerProductPolicy(env,priceId,'paddle');
  if(paddleId(price.product_id,'pro')!==policy.productId ||
    ((price.billing_cycle!==null && price.billing_cycle!==undefined)!==(policy.mode==='subscription')))
    throw new ApiError(403,'wrong_product');
  return {priceId,policy};
}
function safeUrl(value: unknown, origin: string): string {
  let url: URL, expected: URL;
  try { url=new URL(str(value,8192)); expected=new URL(origin); }
  catch { throw new ApiError(502,'invalid_checkout_url'); }
  if(url.protocol!=='https:' || url.username || url.password || url.origin!==expected.origin ||
    expected.protocol!=='https:' || expected.hostname.endsWith('.invalid') || expected.username || expected.password)
    throw new ApiError(502,'invalid_checkout_url');
  return url.href;
}
async function checkout(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request,env,'paddle_checkout',5);
  const account=await requireAccount(env,await authenticate(request,env)), input=await body(request);
  const offer=str(input.offer,20);
  if(!['one-time','subscription'].includes(offer)) throw new ApiError(400,'invalid_offer');
  const mode=environment(env), priceId=paddleId(offer==='one-time'?env.PADDLE_ONE_TIME_PRICE_ID:env.PADDLE_SUBSCRIPTION_PRICE_ID,'pri');
  const policy=providerProductPolicy(env,priceId,'paddle');
  if(policy.mode!==(offer==='one-time'?'perpetual':'subscription')) throw new ApiError(503,'invalid_policy');
  let owner=await env.DB.prepare('SELECT customer_id,environment FROM paddle_accounts WHERE account_id=?').bind(account.id).first<{customer_id:string;environment:string}>();
  if(!owner) {
    const customer=object((await api(env,'customers',{email:account.email,custom_data:{looksy_account_id:account.id}})).data);
    await env.DB.prepare('INSERT OR IGNORE INTO paddle_accounts(account_id,customer_id,environment) VALUES(?,?,?)')
      .bind(account.id,paddleId(customer.id,'ctm'),mode).run();
    owner=await env.DB.prepare('SELECT customer_id,environment FROM paddle_accounts WHERE account_id=?').bind(account.id).first<{customer_id:string;environment:string}>();
  }
  if(!owner || owner.environment!==mode) throw new ApiError(503,'wrong_billing_environment');
  const transaction=object((await api(env,'transactions',{items:[{price_id:priceId,quantity:1}],customer_id:owner.customer_id,
    collection_mode:'automatic',custom_data:{looksy_account_id:account.id}})).data);
  const transactionId=paddleId(transaction.id,'txn');
  if(transaction.customer_id!==owner.customer_id) throw new ApiError(502,'invalid_paddle_response');
  const url=safeUrl(object(transaction.checkout).url,env.PADDLE_CHECKOUT_ORIGIN);
  await requireAccount(env,await authenticate(request,env));
  await env.DB.prepare('INSERT INTO paddle_checkouts(transaction_id,account_id,customer_id,price_id,environment,created_at) VALUES(?,?,?,?,?,?)')
    .bind(transactionId,account.id,owner.customer_id,priceId,mode,now()).run();
  return json({url});
}
async function portal(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request,env,'paddle_portal',10);
  const account=await requireAccount(env,await authenticate(request,env));
  const owner=await env.DB.prepare('SELECT customer_id,environment FROM paddle_accounts WHERE account_id=?').bind(account.id).first<{customer_id:string;environment:string}>();
  const mode=environment(env);
  if(!owner || owner.environment!==mode) throw new ApiError(404,'billing_account_not_found');
  const result=object((await api(env,`customers/${paddleId(owner.customer_id,'ctm')}/portal-sessions`,{})).data);
  if(result.customer_id!==owner.customer_id) throw new ApiError(502,'invalid_paddle_response');
  const url=safeUrl(object(object(result.urls).general).overview,
    mode==='sandbox'?'https://sandbox-customer-portal.paddle.com':'https://customer-portal.paddle.com');
  await requireAccount(env,await authenticate(request,env));
  return json({url});
}
async function revision(env: ServiceEnv, key: string): Promise<number> {
  const row=await env.DB.prepare(`INSERT INTO refresh_sequences(id,revision) VALUES(?,1)
    ON CONFLICT(id) DO UPDATE SET revision=revision+1 RETURNING revision`).bind(`paddle:${key}`).first<{revision:number}>();
  if(!row) throw new ApiError(503,'storage_unavailable');
  return row.revision;
}
async function refunded(env: ServiceEnv, transactionId: string): Promise<boolean> {
  const result=await api(env,`adjustments?transaction_id=${paddleId(transactionId,'txn')}&per_page=50`);
  if(!Array.isArray(result.data)) throw new ApiError(502,'invalid_paddle_response');
  // Incomplete pagination must fail closed, rather than miss a refund/chargeback.
  if(object(object(result.meta).pagination).has_more===true) throw new ApiError(403,'billing_review_required');
  const rows=result.data.map(object);
  if(rows.some(row=>row.transaction_id!==transactionId)) throw new ApiError(502,'invalid_paddle_response');
  // Partial refunds are reviewed by support before granting access again. A
  // later paid subscription period is evaluated against its own transaction.
  return rows.some(row=>row.status==='approved' && ['refund','chargeback'].includes(String(row.action)));
}
async function saveLicense(env: ServiceEnv, mapping: Mapping, transaction: Record<string,unknown>, rev: number, sub?: Record<string,unknown>): Promise<License> {
  const mode=environment(env), time=now();
  if(mapping.environment!==mode || transaction.customer_id!==mapping.customer_id ||
    (sub && sub.customer_id!==mapping.customer_id)) throw new ApiError(403,'wrong_billing_owner');
  const account=await env.DB.prepare('SELECT disabled FROM accounts WHERE id=?').bind(mapping.account_id).first<{disabled:number}>();
  if(!account || account.disabled) throw new ApiError(403,'account_disabled');
  const {priceId,policy}=items(env,sub?sub.items:transaction.items);
  if((policy.mode==='subscription')!==Boolean(sub)) throw new ApiError(403,'wrong_product');
  let paidTransaction=transaction;
  if(sub) {
    // Latest completed billing transaction avoids an old refund permanently
    // disabling a later paid renewal. The API sort is explicit and bounded.
    const listing=await api(env,`transactions?subscription_id=${paddleId(sub.id,'sub')}&status=completed&order_by=billed_at[DESC]&per_page=1`);
    if(!Array.isArray(listing.data) || listing.data.length!==1) throw new ApiError(403,'payment_required');
    paidTransaction=object(listing.data[0]);
    if(paidTransaction.subscription_id!==sub.id || paidTransaction.customer_id!==mapping.customer_id || paidTransaction.status!=='completed')
      throw new ApiError(502,'invalid_paddle_response');
    const paid=items(env,paidTransaction.items);
    if(paid.priceId!==priceId || paid.policy.mode!=='subscription') throw new ApiError(403,'payment_required');
  }
  const paidId=paddleId(paidTransaction.id,'txn');
  const reversed=await refunded(env,paidId);
  const status=transaction.status==='completed' && !reversed?'active':'disabled';
  const customerId=`paddle:${mode}:${mapping.customer_id}`, itemId=mapping.license_id;
  let expires: number|null=null, subscriptionStatus='inactive';
  if(sub) {
    subscriptionStatus=({active:'active',trialing:'on_trial',past_due:'past_due',canceled:'inactive',paused:'inactive'} as Record<string,string>)[String(sub.status)] ?? 'inactive';
    const period=sub.current_billing_period===null?null:object(sub.current_billing_period);
    expires=period?timestamp(period.ends_at):time;
    if(expires===null) throw new ApiError(502,'invalid_paddle_response');
    if(sub.scheduled_change) {
      const change=object(sub.scheduled_change);
      if(['cancel','pause'].includes(String(change.action))) {
        const end=timestamp(change.effective_at);
        if(end===null) throw new ApiError(502,'invalid_paddle_response');
        expires=Math.min(expires,end);
      }
    }
    // A completed transaction from an earlier period cannot fund a new one.
    const billedAt=timestamp(paidTransaction.billed_at), periodStart=period?timestamp(period.starts_at):null;
    if(billedAt===null || periodStart===null || billedAt<periodStart-300) subscriptionStatus='inactive';
  }
  const statements=[
    env.DB.prepare('INSERT OR IGNORE INTO customers(id,provider_customer_id,created_at) VALUES(?,?,?)')
      .bind(customerId,`paddle:${mode}:${mapping.customer_id}`,time),
    env.DB.prepare(`INSERT INTO licenses(id,customer_id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,expires_at,checked_at,refresh_revision)
      VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET variant_id=excluded.variant_id,plan=excluded.plan,mode=excluded.mode,
      major=excluded.major,features=excluded.features,device_limit=excluded.device_limit,
      status=CASE WHEN EXISTS(SELECT 1 FROM privacy_tombstones WHERE customer_id=licenses.customer_id) THEN 'deleted' ELSE excluded.status END,
      expires_at=excluded.expires_at,checked_at=excluded.checked_at,refresh_revision=excluded.refresh_revision
      WHERE licenses.refresh_revision<excluded.refresh_revision`)
      .bind(mapping.license_id,customerId,mapping.transaction_id,itemId,priceId,policy.plan,policy.mode,policy.major,
        JSON.stringify(policy.features),policy.deviceLimit,status,expires,time,rev),
    env.DB.prepare(`INSERT OR IGNORE INTO paddle_licenses(license_id,account_id,customer_id,transaction_id,subscription_id,environment) VALUES(?,?,?,?,?,?)`)
      .bind(mapping.license_id,mapping.account_id,mapping.customer_id,mapping.transaction_id,mapping.subscription_id,mode),
    env.DB.prepare('INSERT OR IGNORE INTO account_licenses(license_id,account_id,linked_at) VALUES(?,?,?)').bind(mapping.license_id,mapping.account_id,time),
  ];
  if(sub) statements.push(env.DB.prepare(`INSERT INTO subscriptions(id,order_item_id,variant_id,status,ends_at,checked_at,refresh_revision)
    VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET variant_id=excluded.variant_id,status=excluded.status,ends_at=excluded.ends_at,
    checked_at=excluded.checked_at,refresh_revision=excluded.refresh_revision WHERE subscriptions.refresh_revision<excluded.refresh_revision`)
    .bind(`paddle:${mode}:${sub.id}`,itemId,priceId,subscriptionStatus,expires,time,rev));
  statements.push(env.DB.prepare('UPDATE paddle_checkouts SET fulfilled_at=?,checked_at=? WHERE transaction_id=?').bind(time,time,mapping.transaction_id));
  try { await env.DB.batch(statements); }
  catch(error) { if(String(error).includes('account_disabled')) throw new ApiError(403,'account_disabled'); throw error; }
  const stored=await env.DB.prepare('SELECT * FROM licenses WHERE id=?').bind(mapping.license_id).first<License>();
  if(!stored) throw new ApiError(503,'storage_unavailable');
  return stored;
}
async function syncTransaction(env: ServiceEnv, transactionId: string): Promise<void> {
  const checkout=await env.DB.prepare('SELECT * FROM paddle_checkouts WHERE transaction_id=?').bind(transactionId).first<Checkout>();
  if(!checkout) return; // Never fulfill client-supplied metadata or email-only purchases.
  if(checkout.environment!==environment(env)) throw new ApiError(403,'wrong_billing_environment');
  const rev=await revision(env,'provider');
  const transaction=await resource(env,'transactions',transactionId,'txn');
  await env.DB.prepare('UPDATE paddle_checkouts SET checked_at=? WHERE transaction_id=?').bind(now(),transactionId).run();
  if(transaction.status!=='completed') {
    // Retire terminal failures; pending checkout polling backs off for an hour.
    if(transaction.status==='canceled') await env.DB.prepare('UPDATE paddle_checkouts SET fulfilled_at=? WHERE transaction_id=?').bind(now(),transactionId).run();
    return;
  }
  const {priceId,policy}=items(env,transaction.items);
  if(priceId!==checkout.price_id || transaction.customer_id!==checkout.customer_id) throw new ApiError(403,'wrong_product');
  const subId=policy.mode==='subscription'?paddleId(transaction.subscription_id,'sub'):null;
  const mapping:Mapping={license_id:`paddle:${environment(env)}:${subId??transactionId}`,account_id:checkout.account_id,
    customer_id:checkout.customer_id,transaction_id:transactionId,subscription_id:subId,environment:checkout.environment};
  await saveLicense(env,mapping,transaction,rev,subId?await resource(env,'subscriptions',subId,'sub'):undefined);
}
export async function syncPaddleLicense(env: ServiceEnv, licenseId: string): Promise<License> {
  const mapping=await env.DB.prepare('SELECT * FROM paddle_licenses WHERE license_id=?').bind(licenseId).first<Mapping>();
  if(!mapping) throw new ApiError(404,'license_not_found');
  const rev=await revision(env,'provider');
  return saveLicense(env,mapping,await resource(env,'transactions',mapping.transaction_id,'txn'),rev,
    mapping.subscription_id?await resource(env,'subscriptions',mapping.subscription_id,'sub'):undefined);
}
export async function reconcilePaddleCheckouts(env: ServiceEnv): Promise<void> {
  const rows=await env.DB.prepare('SELECT transaction_id FROM paddle_checkouts WHERE fulfilled_at IS NULL AND checked_at<? AND created_at>? ORDER BY checked_at LIMIT 10')
    .bind(now()-3600,now()-30*86400).all<{transaction_id:string}>();
  for(const row of rows.results) {
    try { await syncTransaction(env,row.transaction_id); }
    catch { console.error(JSON.stringify({event:'paddle_checkout_reconcile_failed'})); }
  }
}
export async function processPaddleEvent(env: ServiceEnv, event: string, resourceId: string): Promise<void> {
  if(event.startsWith('paddle:')) event=event.slice(7);
  if(event.startsWith('transaction.')) {
    await syncTransaction(env,paddleId(resourceId,'txn'));
    // Renewal events have no original checkout row; refresh their existing license.
    const transaction=await resource(env,'transactions',resourceId,'txn');
    if(transaction.subscription_id) {
      const mapping=await env.DB.prepare('SELECT license_id FROM paddle_licenses WHERE subscription_id=?').bind(paddleId(transaction.subscription_id,'sub')).first<{license_id:string}>();
      if(mapping) await syncPaddleLicense(env,mapping.license_id);
    }
  } else if(event.startsWith('subscription.')) {
    const mapping=await env.DB.prepare('SELECT license_id FROM paddle_licenses WHERE subscription_id=?').bind(paddleId(resourceId,'sub')).first<{license_id:string}>();
    if(mapping) await syncPaddleLicense(env,mapping.license_id);
  } else if(event.startsWith('adjustment.')) {
    const adjustment=await resource(env,'adjustments',resourceId,'adj');
    const mappings=await env.DB.prepare('SELECT license_id FROM paddle_licenses WHERE transaction_id=? OR subscription_id=?')
      .bind(paddleId(adjustment.transaction_id,'txn'),adjustment.subscription_id??'').all<{license_id:string}>();
    for(const mapping of mappings.results) await syncPaddleLicense(env,mapping.license_id);
  }
}
async function webhook(request: Request, env: ServiceEnv): Promise<Response> {
  const mode=environment(env), raw=await boundedText(request,262144);
  const fields=(request.headers.get('Paddle-Signature')??'').split(';').map(field=>field.trim().split('='));
  const timestamps=fields.filter(([key])=>key==='ts').map(([,value])=>value);
  const signatures=fields.filter(([key])=>key==='h1').map(([,value])=>value);
  if(timestamps.length!==1 || !/^\d{10}$/.test(timestamps[0]) || Math.abs(now()-Number(timestamps[0]))>5 ||
    !signatures.length || signatures.length>4) throw new ApiError(401,'invalid_webhook_signature');
  const key=await hmacKey(env.PADDLE_WEBHOOK_SECRET??''), message=new TextEncoder().encode(`${timestamps[0]}:${raw}`);
  let verified=false;
  for(const signature of signatures) if(/^[a-f0-9]{64}$/.test(signature)) {
    const bytes=Uint8Array.from(signature.match(/../g)!,pair=>parseInt(pair,16));
    verified=(await crypto.subtle.verify('HMAC',key,bytes,message))||verified;
  }
  if(!verified) throw new ApiError(401,'invalid_webhook_signature');
  let payload:Record<string,unknown>;
  try { payload=object(JSON.parse(raw)); } catch { throw new ApiError(400,'invalid_webhook'); }
  const eventId=paddleId(payload.event_id,'evt'), event=str(payload.event_type,100), data=object(payload.data);
  if(!/^(transaction|subscription|adjustment)\.[a-z_]+$/.test(event)) return json({ok:true});
  const prefix=event.split('.')[0]==='transaction'?'txn':event.split('.')[0]==='subscription'?'sub':'adj';
  const resourceId=paddleId(data.id,prefix), digest=await hash(`paddle:${mode}:${eventId}`);
  await env.DB.prepare('INSERT OR IGNORE INTO webhook_receipts(digest,event,resource_id,received_at) VALUES(?,?,?,?)')
    .bind(digest,`paddle:${event}`,resourceId,now()).run();
  const receipt=await env.DB.prepare('SELECT processed_at FROM webhook_receipts WHERE digest=?').bind(digest).first<{processed_at:number|null}>();
  if(receipt?.processed_at) return json({ok:true});
  try {
    await processPaddleEvent(env,event,resourceId);
    await env.DB.prepare('UPDATE webhook_receipts SET processed_at=?,last_error_code=NULL WHERE digest=?').bind(now(),digest).run();
  } catch {
    await env.DB.prepare(`UPDATE webhook_receipts SET attempts=attempts+1,next_retry_at=?,last_error_code='provider_refresh_failed' WHERE digest=?`)
      .bind(now()+60,digest).run();
    throw new ApiError(502,'billing_unavailable');
  }
  return json({ok:true});
}
export async function paddleRoute(request: Request, env: ServiceEnv, path: string): Promise<Response|null> {
  if(request.method!=='POST') return null;
  if(path==='/v1/webhooks/paddle') return webhook(request,env);
  if(path==='/v1/billing/checkout') return checkout(request,env);
  if(path==='/v1/billing/portal') return portal(request,env);
  return null;
}
