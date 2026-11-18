import { env } from 'cloudflare:workers';
import { applyD1Migrations } from 'cloudflare:test';
import { beforeAll,beforeEach,afterEach,describe,it,expect,vi } from 'vitest';
import { exportPKCS8,generateKeyPair,jwtVerify } from 'jose';
import worker from '../src/index';
import { syncPaddleLicense } from '../src/paddle';
import { DAY,hash,hex,hmacKey,issueLease,now,type Installation,type ServiceEnv } from '../src/core';

const pid=(prefix:string,letter='a')=>`${prefix}_${letter.repeat(26)}`;
const customer=pid('ctm'),txn=pid('txn'),sub=pid('sub'),one=pid('pri'),recurring=pid('pri','b'),product=pid('pro');
const credential='p'.repeat(43),account='account:paddle-test';
let service:ServiceEnv,publicKey:CryptoKey,transaction:Record<string,unknown>,subscription:Record<string,unknown>;
let adjustments:unknown[],checkoutUrl:string,latest:Record<string,unknown>,fetchMock:ReturnType<typeof vi.fn>;
function line(price=one,subscriptionMode=false) {return [{quantity:1,price:{id:price,product_id:product,billing_cycle:subscriptionMode?{interval:'month',frequency:1}:null}}];}
async function call(path:string,input:unknown={},token=credential) {
 return worker.fetch(new Request('https://accounts.looksy.test'+path,{method:'POST',headers:{'Content-Type':'application/json',Authorization:`Bearer ${token}`},body:JSON.stringify(input)}),service);
}
async function checkout(offer='one-time') {const response=await call('/v1/billing/checkout',{offer});expect(response.status).toBe(200);return response.json();}
async function webhook(event='transaction.completed',resourceId=txn,eventId=pid('evt'),timestamp=now()) {
 const raw=JSON.stringify({event_id:eventId,event_type:event,data:{id:resourceId}});
 const signature=hex(await crypto.subtle.sign('HMAC',await hmacKey(service.PADDLE_WEBHOOK_SECRET!),new TextEncoder().encode(`${timestamp}:${raw}`)));
 return worker.fetch(new Request('https://accounts.looksy.test/v1/webhooks/paddle',{method:'POST',headers:{'Content-Type':'application/json','Paddle-Signature':`ts=${timestamp};h1=${signature}`},body:raw}),service);
}
async function stored() {return env.DB.prepare('SELECT * FROM licenses WHERE id LIKE ?').bind('paddle:%').first<Record<string,unknown>>();}
async function prepareSubscription() {
 transaction={...transaction,subscription_id:sub,items:line(recurring,true)}; latest=transaction;
 await checkout('subscription');expect((await webhook()).status).toBe(200);
}
async function device() {return (await env.DB.prepare('SELECT * FROM installations WHERE credential_hash=?').bind(await hash(credential)).first<Installation>())!;}
async function activate() {const license=await stored();return call('/v1/account/license',{licenseId:license!.id});}
beforeAll(async()=>{
 await applyD1Migrations(env.DB,env.TEST_MIGRATIONS);
 const pair=await generateKeyPair('EdDSA',{extractable:true});publicKey=pair.publicKey;
 service={...env,ISSUER:'https://accounts.looksy.test',ENTITLEMENT_PRIVATE_KEY:await exportPKCS8(pair.privateKey),
  LEMON_API_KEY:'legacy-test',LEMON_WEBHOOK_SECRET:'legacy-secret-'.repeat(4),RATE_LIMIT_SECRET:'rate-secret-'.repeat(4),
  PADDLE_API_KEY:'pdl_sdbx_apikey_test-only',PADDLE_WEBHOOK_SECRET:'pdl_ntfset_test-secret-'.repeat(3),
  PADDLE_ENVIRONMENT:'sandbox',PADDLE_CHECKOUT_ORIGIN:'https://checkout.looksy.test',PADDLE_ONE_TIME_PRICE_ID:one,PADDLE_SUBSCRIPTION_PRICE_ID:recurring,
  PADDLE_PRODUCT_POLICY:JSON.stringify({[one]:{productId:product,plan:'Studio',mode:'perpetual',major:0,features:['editor','agent'],deviceLimit:2},
   [recurring]:{productId:product,plan:'Studio monthly',mode:'subscription',major:0,features:['editor','agent'],deviceLimit:2}})};
});
beforeEach(async()=>{
 for(const table of ['paddle_licenses','paddle_checkouts','paddle_accounts','email_challenges','vip_redemptions','vip_codes','account_licenses','audit','staff','rate_buckets','webhook_receipts','installations','licenses','subscriptions','privacy_tombstones','customers','refresh_sequences','accounts']) await env.DB.prepare(`DELETE FROM ${table}`).run();
 const time=now();
 await env.DB.prepare('INSERT INTO accounts(id,email,trial_ends_at,created_at) VALUES(?,?,?,?)').bind(account,'paddle-owner@example.test',time+DAY,time).run();
 await call('/v1/trial',{credential});await env.DB.prepare('UPDATE installations SET account_id=?,account_session_expires_at=?').bind(account,time+30*DAY).run();
 transaction={id:txn,status:'completed',customer_id:customer,subscription_id:null,items:line(),checkout:{url:'https://checkout.looksy.test/?_ptxn='+txn},billed_at:new Date(time*1000).toISOString()};
 subscription={id:sub,status:'active',customer_id:customer,items:line(recurring,true),current_billing_period:{starts_at:new Date((time-60)*1000).toISOString(),ends_at:new Date((time+30*DAY)*1000).toISOString()},scheduled_change:null};
 latest=transaction;adjustments=[];checkoutUrl='https://checkout.looksy.test/?_ptxn='+txn;
 fetchMock=vi.fn(async(url:URL|string,init?:RequestInit)=>{
  const uri=new URL(String(url));expect(uri.hostname).toBe('sandbox-api.paddle.com');
  expect(new Headers(init?.headers).get('Paddle-Version')).toBe('1');
  let data:unknown;
  if(uri.pathname==='/customers') data={id:customer};
  else if(uri.pathname==='/transactions' && init?.method==='POST') data={...transaction,checkout:{url:checkoutUrl}};
  else if(uri.pathname==='/transactions') data=[latest];
  else if(uri.pathname===`/transactions/${txn}`) data=transaction;
  else if(uri.pathname===`/subscriptions/${sub}`) data=subscription;
  else if(uri.pathname==='/adjustments') return Response.json({data:adjustments,meta:{pagination:{has_more:false}}});
  else if(uri.pathname===`/adjustments/${pid('adj')}`) data={id:pid('adj'),transaction_id:txn,subscription_id:transaction.subscription_id};
  else if(uri.pathname===`/customers/${customer}/portal-sessions`) data={customer_id:customer,urls:{general:{overview:'https://sandbox-customer-portal.paddle.com/test?token=private'}}};
  else throw new Error(`Unexpected fixture endpoint ${uri.pathname}`);
  return Response.json({data});
 });vi.stubGlobal('fetch',fetchMock);
});
afterEach(()=>vi.unstubAllGlobals());
describe('Paddle account-bound billing',()=>{
 it('creates server-bound checkout without leaking API credentials and requires verified account',async()=>{
  const response=await checkout();expect(response).toEqual({url:checkoutUrl});
  const row=await env.DB.prepare('SELECT * FROM paddle_checkouts').first();expect(row!.account_id).toBe(account);
  expect(await stored()).toBeNull();
  const create=fetchMock.mock.calls.find(([url,init])=>String(url).endsWith('/transactions')&&init?.method==='POST')!;
  expect(JSON.parse(String(create[1]!.body)).customer_id).toBe(customer);
  await env.DB.prepare('UPDATE installations SET account_id=NULL').run();
  expect((await call('/v1/billing/checkout',{offer:'one-time'})).status).toBe(401);
 });
 it('fulfills only completed stored checkout and issues verifiable device-bound perpetual lease',async()=>{
  await checkout();expect((await webhook()).status).toBe(200);expect((await activate()).status).toBe(200);
  const license=await stored();expect(license!.mode).toBe('perpetual');expect(license!.status).toBe('active');
  const {payload}=await jwtVerify(await issueLease(service,await device()),publicKey);expect(payload.mode).toBe('perpetual');expect(payload.device).toBe(await hash(credential));
  expect((await webhook()).status).toBe(200);expect((await env.DB.prepare('SELECT count(*) AS n FROM licenses').first())!.n).toBe(1);
 });
 it('does not fulfill fabricated metadata or a pending checkout',async()=>{
  transaction={...transaction,custom_data:{looksy_account_id:account}};
  expect((await webhook()).status).toBe(200);expect(await stored()).toBeNull();
  await checkout();transaction.status='paid';expect((await webhook('transaction.updated',txn,pid('evt','b'))).status).toBe(200);expect(await stored()).toBeNull();
 });
 it('rejects wrong customer, product, price and quantity without granting a license',async()=>{
  await checkout();const original={...transaction};
  for(const patch of [{customer_id:pid('ctm','b')},{items:[{quantity:2,price:{id:one,product_id:product,billing_cycle:null}}]},
    {items:[{quantity:1,price:{id:one,product_id:pid('pro','b'),billing_cycle:null}}]}, {items:line(recurring,true)}]) {
   transaction={...original,...patch};expect((await webhook()).status).toBe(502);expect(await stored()).toBeNull();
  }
 });
 it('rejects bad, expired and duplicate-timestamp webhook signatures',async()=>{
  expect((await webhook('transaction.completed',txn,pid('evt'),now()-10)).status).toBe(401);
  for(const signature of ['ts=0;h1='+'a'.repeat(64),`ts=${now()};ts=${now()};h1=${'a'.repeat(64)}`,`ts=${now()};h1=${'a'.repeat(64)}`]) {
   const response=await worker.fetch(new Request('https://accounts.looksy.test/v1/webhooks/paddle',{method:'POST',headers:{'Paddle-Signature':signature},body:'{}'}),service);expect(response.status).toBe(401);
  }
 });
 it('rejects mixed sandbox/live configuration and unapproved checkout redirects',async()=>{
  const prior=service;service={...service,PADDLE_ENVIRONMENT:'live'};expect((await call('/v1/billing/checkout',{offer:'one-time'})).status).toBe(503);service=prior;
  checkoutUrl='https://checkout.looksy.test.evil.test/pay';expect((await call('/v1/billing/checkout',{offer:'one-time'})).status).toBe(502);
  expect(await env.DB.prepare('SELECT * FROM paddle_checkouts').first()).toBeNull();
 });
 it('creates portal sessions for exact account customer and does not expose another account',async()=>{
  expect((await call('/v1/billing/portal')).status).toBe(404);await checkout();
  const response=await call('/v1/billing/portal');expect(response.status).toBe(200);expect((await response.json() as {url:string}).url).toContain('sandbox-customer-portal.paddle.com/');
  await env.DB.prepare('DELETE FROM paddle_accounts').run();expect((await call('/v1/billing/portal')).status).toBe(404);
 });
 it('revokes refunded/charged-back access while preserving explicit staff blocks',async()=>{
  await checkout();await webhook();await activate();const license=await stored();
  adjustments=[{transaction_id:txn,status:'approved',action:'refund',type:'full'}];await syncPaddleLicense(service,String(license!.id));
  expect((await stored())!.status).toBe('disabled');await expect(issueLease(service,await device())).rejects.toThrow('license_inactive');
  adjustments=[];await env.DB.prepare('UPDATE licenses SET blocked=1').run();await syncPaddleLicense(service,String(license!.id));expect((await stored())!.blocked).toBe(1);
 });
 it('persists failed fulfillment for scheduled retry without granting access',async()=>{
  await checkout();vi.stubGlobal('fetch',async()=>new Response('provider-secret',{status:503}));
  expect((await webhook()).status).toBe(502);const receipt=await env.DB.prepare('SELECT * FROM webhook_receipts').first();expect(receipt!.processed_at).toBeNull();expect(receipt!.attempts).toBe(1);expect(receipt!.event).toBe('paddle:transaction.completed');expect(await stored()).toBeNull();
 });
 it('bounds subscription leases by paid period and scheduled cancellation, denies immediate pause',async()=>{
  await prepareSubscription();expect((await activate()).status).toBe(200);
  subscription.scheduled_change={action:'cancel',effective_at:new Date((now()+3600)*1000).toISOString()};await syncPaddleLicense(service,String((await stored())!.id));
  const {payload}=await jwtVerify(await issueLease(service,await device()),publicKey);expect(payload.exp).toBeLessThanOrEqual(now()+3600);
  subscription.status='paused';subscription.current_billing_period=null;await syncPaddleLicense(service,String((await stored())!.id));
  await expect(issueLease(service,await device())).rejects.toThrow('subscription_inactive');
 });
 it('does not extend a past-due subscription into an unfunded new period',async()=>{
  await prepareSubscription();await activate();subscription.status='past_due';
  subscription.current_billing_period={starts_at:new Date((now()+DAY)*1000).toISOString(),ends_at:new Date((now()+31*DAY)*1000).toISOString()};
  await syncPaddleLicense(service,String((await stored())!.id));await expect(issueLease(service,await device())).rejects.toThrow('subscription_inactive');
 });
 it('denies an unpaid price change and adjustment pagination that requires manual review',async()=>{
  await prepareSubscription();await activate();const licenseId=String((await stored())!.id);
  latest={...transaction,items:line()};
  await expect(syncPaddleLicense(service,licenseId)).rejects.toThrow('payment_required');
  latest=transaction;const prior=globalThis.fetch;
  vi.stubGlobal('fetch',async(url:URL|string,init?:RequestInit)=>new URL(String(url)).pathname==='/adjustments'
    ?Response.json({data:[],meta:{pagination:{has_more:true}}}):prior(url,init));
  const response=await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'});
  expect(response.status).toBe(403);expect(await response.json()).toEqual({error:'billing_review_required'});
 });
 it('rolls back fulfillment if the account is deleted while provider reads are in flight',async()=>{
  await checkout();const prior=globalThis.fetch;
  vi.stubGlobal('fetch',async(url:URL|string,init?:RequestInit)=>{
   if(new URL(String(url)).pathname==='/adjustments') await env.DB.prepare('UPDATE accounts SET disabled=1 WHERE id=?').bind(account).run();
   return prior(url,init);
  });
  expect((await webhook()).status).toBe(502);expect(await stored()).toBeNull();
  expect(await env.DB.prepare('SELECT * FROM paddle_licenses').first()).toBeNull();
 });
 it('reconciles a missed completion webhook and retires canceled pending checkouts',async()=>{
  await checkout();
  await env.DB.prepare('UPDATE paddle_checkouts SET checked_at=?').bind(now()-7200).run();
  await worker.scheduled({} as ScheduledController,service);
  expect((await stored())!.status).toBe('active');
  // A second synthetic abandoned transaction is terminal and never grants access.
  await env.DB.prepare('DELETE FROM paddle_licenses').run();await env.DB.prepare('DELETE FROM account_licenses').run();
  await env.DB.prepare('DELETE FROM licenses').run();await env.DB.prepare('UPDATE paddle_checkouts SET fulfilled_at=NULL,checked_at=0').run();
  transaction.status='canceled';await worker.scheduled({} as ScheduledController,service);
  expect(await stored()).toBeNull();expect((await env.DB.prepare('SELECT fulfilled_at FROM paddle_checkouts').first())!.fulfilled_at).toBeTruthy();
 });
 it('does not let an older concurrent provider response overwrite a newer subscription state',async()=>{
  await prepareSubscription();const licenseId=String((await stored())!.id),prior=globalThis.fetch;
  let release!:()=>void,entered!:()=>void;const wait=new Promise<void>(resolve=>{release=resolve;});const reached=new Promise<void>(resolve=>{entered=resolve;});let first=true;
  vi.stubGlobal('fetch',async(url:URL|string,init?:RequestInit)=>{
   if(String(url).endsWith('/subscriptions/'+sub)&&first) {first=false;const old=structuredClone(subscription);entered();await wait;return Response.json({data:old});}
   return prior(url,init);
  });
  const older=syncPaddleLicense(service,licenseId);await reached;subscription.status='paused';subscription.current_billing_period=null;
  await syncPaddleLicense(service,licenseId);release();await older;
  expect((await env.DB.prepare('SELECT status FROM subscriptions').first())!.status).toBe('inactive');
 });
});
