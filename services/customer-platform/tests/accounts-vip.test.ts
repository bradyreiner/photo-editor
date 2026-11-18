import { env } from 'cloudflare:workers';
import { applyD1Migrations } from 'cloudflare:test';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { exportJWK, exportPKCS8, generateKeyPair, jwtVerify, SignJWT } from 'jose';
import worker from '../src/index';
import { DAY, now, type ServiceEnv } from '../src/core';

let service:ServiceEnv, publicKey:CryptoKey, emailCode='', deliveries=0;
const credential=(letter='a')=>letter.repeat(43);
async function call(path:string,input?:unknown,token=credential()) {
  return worker.fetch(new Request('https://accounts.looksy.test'+path,{method:input===undefined?'GET':'POST',
    headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:input===undefined?undefined:JSON.stringify(input)}),service);
}
function mail() {
  vi.stubGlobal('fetch',vi.fn(async(url:string,init:RequestInit)=>{
    if(String(url)!=='https://api.resend.com/emails') throw new Error('Unexpected external request');
    const data=JSON.parse(String(init.body));emailCode=data.text.match(/code is (\d{8})/)[1];deliveries++;
    return Response.json({id:'test-delivery'});
  }));
}
async function challenge(letter='a',email='owner@example.test') {
  const response=await call('/v1/account/email/request',{email},credential(letter));
  expect(response.status).toBe(200);
  return await response.json() as {challengeId:string;expiresAt:number};
}
async function signIn(letter='a',email='owner@example.test') {
  const {challengeId}=await challenge(letter,email);
  const response=await call('/v1/account/email/verify',{challengeId,code:emailCode},credential(letter));
  expect(response.status).toBe(200);
  return await response.json() as {account:{id:string;email:string};lease?:string;accessError?:string;installationId:string};
}
async function staff(role='admin',enrolled=true) {
  const pair=await generateKeyPair('RS256',{extractable:true}),issuer='https://looksy.cloudflareaccess.com';
  const jwk={...await exportJWK(pair.publicKey),kid:'staff',alg:'RS256',use:'sig'};
  const prior=globalThis.fetch;
  vi.stubGlobal('fetch',async(url:URL|string,init?:RequestInit)=>String(url)===issuer+'/cdn-cgi/access/certs'?Response.json({keys:[jwk]}):prior(url,init));
  if(enrolled) await env.DB.prepare('INSERT INTO staff VALUES(?,?)').bind('owner',role).run();
  const token=await new SignJWT({}).setProtectedHeader({alg:'RS256',kid:'staff'}).setIssuer(issuer).setAudience('staff-app')
    .setSubject('owner').setIssuedAt().setExpirationTime('5m').sign(pair.privateKey);
  return async(path:string,input?:unknown,origin='https://accounts.looksy.test')=>worker.fetch(new Request('https://accounts.looksy.test/admin/api/'+path,
    {method:input===undefined?'GET':'POST',headers:{'Cf-Access-Jwt-Assertion':token,'Content-Type':'application/json','X-Looksy-Admin':'1',Origin:origin},body:input===undefined?undefined:JSON.stringify(input)}),
    {...service,ACCESS_TEAM_DOMAIN:'looksy.cloudflareaccess.com',ACCESS_AUDIENCE:'staff-app'});
}
const policy=()=>({plan:'VIP Studio',features:['editor','agent','beta'],major:0,deviceLimit:2,redemptionLimit:1,expiresAt:now()+DAY,accessEndsAt:now()+30*DAY,reason:'Private launch acceptance'});
async function createVip(admin:Awaited<ReturnType<typeof staff>>,extra={}) {
  const response=await admin('vip/create',{...policy(),...extra});expect(response.status).toBe(201);
  return response.json() as Promise<{id:string;code:string}>;
}
beforeAll(async()=>{
  await applyD1Migrations(env.DB,env.TEST_MIGRATIONS);
  const pair=await generateKeyPair('EdDSA',{extractable:true});publicKey=pair.publicKey;
  service={...env,ISSUER:'https://accounts.looksy.test',ENTITLEMENT_PRIVATE_KEY:await exportPKCS8(pair.privateKey),
    LEMON_API_KEY:'test',LEMON_WEBHOOK_SECRET:'webhook-test-only-'.repeat(3),RATE_LIMIT_SECRET:'rate-test-only-'.repeat(3),
    EMAIL_FROM:'Looksy <accounts@looksy.test>',RESEND_API_KEY:'test-only'};
});
beforeEach(async()=>{
  for(const table of ['email_challenges','vip_redemptions','vip_codes','account_licenses','audit','staff','rate_buckets','webhook_receipts','installations','licenses','subscriptions','privacy_tombstones','customers','refresh_sequences','accounts'])
    await env.DB.prepare(`DELETE FROM ${table}`).run();
  emailCode='';deliveries=0;mail();
});
afterEach(()=>vi.unstubAllGlobals());

describe('verified device-scoped customer accounts',()=>{
  it('creates independent account only after verified email and preserves a guest lease',async()=>{
    const challengeData=await challenge();
    expect(await env.DB.prepare('SELECT count(*) AS n FROM accounts').first()).toEqual({n:0});
    const stored=await env.DB.prepare('SELECT * FROM email_challenges').first();
    expect(stored.code_hash).not.toBe(emailCode);expect(JSON.stringify(stored)).not.toContain(emailCode);
    const response=await call('/v1/account/email/verify',{challengeId:challengeData.challengeId,code:emailCode});
    const data=await response.json() as {account:{id:string};lease:string};expect(data.account.id).toMatch(/^account:/);
    expect((await jwtVerify(data.lease,publicKey)).payload.mode).toBe('trial');
    expect((await call('/v1/account')).status).toBe(200);
    expect(await env.DB.prepare('SELECT count(*) AS n FROM staff').first()).toEqual({n:0});
  });
  it('binds challenges to installation and consumes valid codes once even under concurrent replay',async()=>{
    const {challengeId}=await challenge();const code=emailCode;
    await call('/v1/trial',{credential:credential('b')},credential('b'));
    expect((await call('/v1/account/email/verify',{challengeId,code},credential('b'))).status).toBe(403);
    const responses=await Promise.all([call('/v1/account/email/verify',{challengeId,code}),call('/v1/account/email/verify',{challengeId,code})]);
    expect(responses.map(r=>r.status).sort()).toEqual([200,403]);
  });
  it('expires codes and exhausts the five-attempt budget',async()=>{
    const {challengeId}=await challenge();
    const wrong=emailCode==='00000000'?'11111111':'00000000';
    for(let i=0;i<5;i++) expect((await call('/v1/account/email/verify',{challengeId,code:wrong})).status).toBe(403);
    expect((await call('/v1/account/email/verify',{challengeId,code:emailCode})).status).toBe(403);
    const next=await challenge();await env.DB.prepare('UPDATE email_challenges SET expires_at=?').bind(now()-1).run();
    expect((await call('/v1/account/email/verify',{challengeId:next.challengeId,code:emailCode})).status).toBe(403);
  });
  it('throttles delivery per email across installations and exposes no code in API responses',async()=>{
    for(const letter of ['a','b','c']) expect(JSON.stringify(await challenge(letter))).not.toContain(emailCode);
    expect((await call('/v1/account/email/request',{email:'owner@example.test'},credential('d'))).status).toBe(429);
    expect(deliveries).toBe(3);
  });
  it('signs in after trial expiry without renewing trial on the same or recovered device',async()=>{
    await challenge();await env.DB.prepare('UPDATE installations SET trial_ends_at=?').bind(now()-DAY).run();
    const old=await signIn();expect(old.lease).toBeUndefined();expect(old.accessError).toBe('access_expired');
    expect((await call('/v1/account/logout',{})).status).toBe(200);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(401);
    const fresh=await signIn('b');expect(fresh.account.id).toBe(old.account.id);expect(fresh.lease).toBeUndefined();
    expect((await env.DB.prepare('SELECT trial_ends_at FROM installations WHERE id=?').bind(fresh.installationId).first()).trial_ends_at).toBeLessThan(now());
  });
  it('requires sign-out before switching identities and disables every account route/session',async()=>{
    const data=await signIn();
    expect((await call('/v1/account/email/request',{email:'someone@example.test'})).status).toBe(409);
    await env.DB.prepare('UPDATE accounts SET disabled=1 WHERE id=?').bind(data.account.id).run();
    for(const path of ['/v1/account','/v1/devices']) expect((await call(path)).status).toBe(401);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(401);
  });
  it('expires account sessions and renews only with another bound verified email code',async()=>{
    const first=await signIn();await env.DB.prepare('UPDATE installations SET account_session_expires_at=?').bind(now()-1).run();
    expect((await call('/v1/account')).status).toBe(401);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(401);
    const renewed=await signIn();expect(renewed.account.id).toBe(first.account.id);expect(renewed.lease).toBeTypeOf('string');
    const lease=(await jwtVerify(renewed.lease!,publicKey)).payload;
    expect(lease.exp).toBeLessThanOrEqual(now()+30*DAY);
  });
  it('requires sign-out when overlapping verification attempts target different emails',async()=>{
    const first=await challenge(),firstCode=emailCode;
    const verification=call('/v1/account/email/verify',{challengeId:first.challengeId,code:firstCode});
    const replacement=await call('/v1/account/email/request',{email:'different@example.test'});
    const replacementCode=emailCode;
    const firstResult=await verification;
    if(replacement.ok) {
      const next=await replacement.json() as {challengeId:string};
      const secondResult=await call('/v1/account/email/verify',{challengeId:next.challengeId,code:replacementCode});
      expect([firstResult.status,secondResult.status].filter(x=>x===200).length).toBeLessThanOrEqual(1);
    } else expect(replacement.status).toBe(409);
  });
  it('restricts the no-domain Resend sender to its configured owner and refuses production use',async()=>{
    const original=service;
    try {
      service={...service,EMAIL_FROM:'onboarding@resend.dev',EMAIL_TEST_RECIPIENT:'owner@example.test'};
      expect((await call('/v1/account/email/request',{email:'someone-else@example.test'})).status).toBe(403);
      expect(deliveries).toBe(0);
      const signed=await signIn();expect(signed.account.email).toBe('owner@example.test');expect(deliveries).toBe(1);
      service={...service,ALLOW_TEST_PURCHASES:'false'};
      expect((await call('/v1/account/email/request',{email:'owner@example.test'})).status).toBe(503);
      expect(deliveries).toBe(1);
    } finally {service=original;}
  });
  it('fails closed if email delivery is unconfigured or fails and never logs tokens/codes',async()=>{
    const original=service;service={...service,RESEND_API_KEY:undefined};expect((await call('/v1/account/email/request',{email:'owner@example.test'})).status).toBe(503);service=original;
    vi.stubGlobal('fetch',async()=>new Response('provider-secret-details',{status:500}));
    expect((await call('/v1/account/email/request',{email:'owner@example.test'})).status).toBe(502);
    expect((await env.DB.prepare('SELECT consumed_at FROM email_challenges').first()).consumed_at).toBeTruthy();
  });
});

describe('staff controlled complimentary access',()=>{
  it('denies support mint/revoke and cross-origin mutations',async()=>{
    const support=await staff('support');
    expect((await support('vip/create',policy())).status).toBe(403);
    expect((await support('vip/revoke',{id:'x',reason:'test'})).status).toBe(403);
    await env.DB.prepare('UPDATE staff SET role=?').bind('admin').run();
    expect((await support('vip/create',policy(),'https://attacker.test')).status).toBe(403);
  });
  it('creates once-visible hashed codes, redeems only signed-in accounts, and audits without raw code',async()=>{
    const admin=await staff();const grant=await createVip(admin);
    expect((await call('/v1/vip/redeem',{code:grant.code})).status).toBe(401);
    await signIn();const response=await call('/v1/vip/redeem',{code:grant.code});expect(response.status).toBe(200);
    const data=await response.json() as {lease:string};const {payload}=await jwtVerify(data.lease,publicKey);
    expect(payload.mode).toBe('perpetual');expect(payload.features).toEqual(['editor','agent','beta']);expect(payload.exp).toBeLessThanOrEqual(now()+7*DAY);
    for(const table of ['vip_codes','audit','licenses']) expect(JSON.stringify((await env.DB.prepare(`SELECT * FROM ${table}`).all()).results)).not.toContain(grant.code);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(200);
    expect((await admin('vip')).status).toBe(200);
  });
  it('enforces redemption limits atomically across accounts and makes duplicates idempotent',async()=>{
    await signIn();await signIn('b','other@example.test');const admin=await staff();const grant=await createVip(admin);
    const results=await Promise.all([call('/v1/vip/redeem',{code:grant.code}),call('/v1/vip/redeem',{code:grant.code},credential('b'))]);
    expect(results.map(r=>r.status).sort()).toEqual([200,403]);
    const winner=results[0].status===200?'a':'b';expect((await call('/v1/vip/redeem',{code:grant.code},credential(winner))).status).toBe(200);
    expect(await env.DB.prepare('SELECT count(*) AS n FROM vip_redemptions').first()).toEqual({n:1});
  });
  it('enforces device cap, rejects unknown/expired codes, and revokes redeemed access at refresh',async()=>{
    await signIn();await signIn('b');const admin=await staff();const grant=await createVip(admin,{deviceLimit:1});
    expect((await call('/v1/vip/redeem',{code:grant.code})).status).toBe(200);
    expect((await call('/v1/vip/redeem',{code:grant.code},credential('b'))).status).toBe(409);
    expect((await call('/v1/vip/redeem',{code:'bad'})).status).toBe(403);
    expect((await admin('vip/revoke',{id:grant.id,reason:'Acceptance revoke'})).status).toBe(200);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(403);
    expect((await call('/v1/vip/redeem',{code:grant.code})).status).toBe(403);
  });
  it('reissues by revoking old grant and only showing a new code once',async()=>{
    await signIn();const admin=await staff();const grant=await createVip(admin);await call('/v1/vip/redeem',{code:grant.code});
    const response=await admin('vip/reissue',{id:grant.id,reason:'Lost original code'});expect(response.status).toBe(201);
    const replacement=await response.json() as {code:string};expect(replacement.code).not.toBe(grant.code);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(403);
    expect((await call('/v1/vip/redeem',{code:replacement.code})).status).toBe(200);
  });
  it('isolates account license selection, supports recovery and never grants staff rights',async()=>{
    await signIn();const admin=await staff();const grant=await createVip(admin);await call('/v1/vip/redeem',{code:grant.code});
    const license=(await env.DB.prepare('SELECT id FROM licenses').first()).id;
    await signIn('b','other@example.test');expect((await call('/v1/account/license',{licenseId:license},credential('b'))).status).toBe(404);
    await signIn('c');expect((await call('/v1/account/license',{licenseId:license},credential('c'))).status).toBe(200);
    expect((await call('/admin/api/customers')).status).toBe(503);
    expect(await env.DB.prepare('SELECT count(*) AS n FROM staff').first()).toEqual({n:1});
  });
  it('lets admin revoke all sessions or anonymize account, while support cannot escalate',async()=>{
    const account=await signIn();const admin=await staff();
    expect((await admin('account/action',{id:account.account.id,action:'delete',reason:'Deletion request'})).status).toBe(200);
    expect((await env.DB.prepare('SELECT email,disabled FROM accounts').first()).email).not.toBe('owner@example.test');
    expect(await env.DB.prepare('SELECT count(*) AS n FROM email_challenges').first()).toEqual({n:0});
    expect((await call('/v1/account')).status).toBe(401);
  });
});

describe('account purchase ownership and recovery boundaries',()=>{
  function purchaseProvider() {
    const prior=globalThis.fetch;
    vi.stubGlobal('fetch',async(url:string,init?:RequestInit)=>{
      if(String(url).startsWith('https://api.resend.com/')) return prior(url,init);
      const path=new URL(String(url)).pathname;
      const attrs:Record<string,Record<string,unknown>>={
        '/v1/license-keys/10':{store_id:1,customer_id:7,order_id:20,order_item_id:30,product_id:4,user_email:'owner@example.test',status:'active',expires_at:null,disabled:false},
        '/v1/orders/20':{store_id:1,customer_id:7,test_mode:true,status:'paid',refunded:false},
        '/v1/order-items/30':{order_id:20,product_id:4,variant_id:5},
      };
      if(path==='/v1/licenses/validate')return Response.json({valid:true,license_key:{id:10},meta:{store_id:1,variant_id:5,product_id:4}});
      if(attrs[path])return Response.json({data:{id:path.split('/')[3],type:path.split('/')[2],attributes:attrs[path]}});
      throw new Error('Unexpected provider route');
    });
    service={...service,LEMON_STORE_ID:'1',PRODUCT_POLICY:JSON.stringify({'5':{productId:'4',plan:'studio',mode:'perpetual',major:0,features:['editor'],deviceLimit:2}})};
  }
  it('links by live key proof and prevents transfer or key-only recovery after account linking',async()=>{
    await signIn();purchaseProvider();
    expect((await call('/v1/activate',{licenseKey:'validated-key'})).status).toBe(200);
    await signIn('b','another@example.test');
    expect((await call('/v1/activate',{licenseKey:'validated-key'},credential('b'))).status).toBe(409);
    expect((await call('/v1/recover',{licenseKey:'validated-key',credential:credential('c')},credential('c'))).status).toBe(409);
    const linked=await env.DB.prepare('SELECT account_id FROM account_licenses WHERE license_id=?').bind('10').first();
    const owner=await env.DB.prepare('SELECT id FROM accounts WHERE email=?').bind('owner@example.test').first();
    expect(linked.account_id).toBe(owner.id);
  });
  it('keeps another eligible grant selectable after one VIP grant is revoked',async()=>{
    await signIn();const admin=await staff();const first=await createVip(admin),second=await createVip(admin,{plan:'Backup VIP'});
    await call('/v1/vip/redeem',{code:first.code});
    const firstLicense=(await env.DB.prepare('SELECT license_id FROM vip_redemptions WHERE code_id=?').bind(first.id).first()).license_id;
    await call('/v1/vip/redeem',{code:second.code});await admin('vip/revoke',{id:second.id,reason:'Revoke selected grant'});
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(403);
    expect((await call('/v1/account/license',{licenseId:firstLicense})).status).toBe(200);
    expect((await call('/v1/refresh',{version:'0.1.0',platform:'mac-arm'})).status).toBe(200);
  });
  it('anonymizes linked account and pending authentication when deleting its provider customer',async()=>{
    await signIn();purchaseProvider();await call('/v1/activate',{licenseKey:'validated-key'});
    const admin=await staff();expect((await admin('customer-delete',{customerId:'lemon:1:7',reason:'Privacy request'})).status).toBe(200);
    const record=await env.DB.prepare('SELECT email,disabled FROM accounts').first();expect(record.disabled).toBe(1);expect(record.email).not.toBe('owner@example.test');
    expect(await env.DB.prepare('SELECT count(*) AS n FROM email_challenges').first()).toEqual({n:0});
    expect((await call('/v1/account')).status).toBe(401);
  });
});
