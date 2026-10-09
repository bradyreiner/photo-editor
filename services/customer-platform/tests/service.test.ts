import { env } from 'cloudflare:workers';
import { applyD1Migrations } from 'cloudflare:test';
import { beforeAll, beforeEach, afterEach, describe, expect, it, vi } from 'vitest';
import { exportPKCS8, exportJWK, generateKeyPair, jwtVerify, SignJWT } from 'jose';
import worker from '../src/index';
import { syncLicense } from '../src/billing';
import { DAY, hash, hex, hmacKey, issueLease, now, type Installation, type ServiceEnv } from '../src/core';

let service: ServiceEnv, publicKey: CryptoKey;
const credential = (value = 'a') => value.repeat(43);
async function call(path: string, input?: unknown, token?: string, extra = {}) {
  return worker.fetch(new Request('https://accounts.looksy.test' + path, {
    method: input === undefined ? 'GET' : 'POST', headers: { 'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}), ...extra },
    body: input === undefined ? undefined : JSON.stringify(input),
  }), service);
}
async function trial(value = 'a') {
  const response = await call('/v1/trial', { credential: credential(value) });
  expect(response.status).toBe(200);
  return response.json() as Promise<{lease: string; installationId: string}>;
}
function billing({ refunded = false, testMode = true, store = 1, mode = 'perpetual' } = {}) {
  vi.stubGlobal('fetch', vi.fn(async (url: string) => {
    const path = new URL(url).pathname;
    const attributes: Record<string, Record<string, unknown>> = {
      '/v1/license-keys/10': {store_id:store,customer_id:7,order_id:20,order_item_id:30,product_id:4,user_email:'owner@example.test',status:'active',expires_at:null,disabled:false},
      '/v1/orders/20': {store_id:store,customer_id:7,test_mode:testMode,status:refunded?'refunded':'paid',refunded},
      '/v1/order-items/30': {order_id:20,product_id:4,variant_id:mode==='subscription'?6:5},
    };
    if(path==='/v1/licenses/validate') return Response.json({valid:true,license_key:{id:10},meta:{store_id:store,variant_id:mode==='subscription'?6:5,product_id:4}});
    if(path==='/v1/subscriptions') return Response.json({data:[]});
    const parts=path.split('/');
    if(attributes[path]) return Response.json({data:{type:parts[2],id:parts[3],attributes:attributes[path]}});
    throw new Error('Unexpected provider route');
  }));
}
beforeAll(async () => {
  await applyD1Migrations(env.DB, env.TEST_MIGRATIONS);
  const pair = await generateKeyPair('EdDSA', {extractable:true}); publicKey=pair.publicKey;
  service = {...env, ISSUER:'https://accounts.looksy.test', LEMON_STORE_ID:'1',
    ENTITLEMENT_PRIVATE_KEY:await exportPKCS8(pair.privateKey), LEMON_API_KEY:'test-only',
    LEMON_WEBHOOK_SECRET:'webhook-test-secret-'.repeat(3), RATE_LIMIT_SECRET:'rate-test-secret-'.repeat(3),
    PRODUCT_POLICY:JSON.stringify({
      '5':{productId:'4',plan:'studio',mode:'perpetual',major:1,features:['editor','agent'],deviceLimit:2},
      '6':{productId:'4',plan:'studio-monthly',mode:'subscription',major:1,features:['editor','agent'],deviceLimit:2},
    }),
  };
});
beforeEach(async () => {
  for (const table of ['audit','staff','rate_buckets','webhook_receipts','installations','licenses','subscriptions','privacy_tombstones','customers','refresh_sequences']) await env.DB.prepare(`DELETE FROM ${table}`).run();
});
afterEach(() => vi.unstubAllGlobals());

describe('guest and device boundary', () => {
  it('issues a verifiable device-bound lease and repeated starts do not reset trial', async () => {
    const first=await trial(); const second=await trial();
    const {payload}=await jwtVerify(first.lease,publicKey,{issuer:service.ISSUER,audience:'looksy-desktop',algorithms:['EdDSA']});
    expect(payload.device).toBe(await hash(credential())); expect(payload.mode).toBe('trial');
    expect(payload.exp! - payload.iat!).toBe(14*DAY);
    expect(second.installationId).toBe(first.installationId);
    expect((await jwtVerify(second.lease,publicKey)).payload.exp).toBe(payload.exp);
  });
  it('cannot restart an expired trial', async () => {
    await trial(); await env.DB.prepare('UPDATE installations SET trial_ends_at=?').bind(now()-1).run();
    expect((await call('/v1/trial',{credential:credential()})).status).toBe(403);
  });
  it('rejects a revoked credential without issuing another trial', async () => {
    const first=await trial();
    expect((await call('/v1/devices/revoke',{id:first.installationId},credential())).status).toBe(200);
    expect((await call('/v1/refresh',{version:'1.0.0',platform:'mac-arm'},credential())).status).toBe(401);
    expect((await call('/v1/trial',{credential:credential()})).status).toBe(403);
  });
  it('does not let one guest revoke another guest', async () => {
    await trial(); const other=await trial('b');
    expect((await call('/v1/devices/revoke',{id:other.installationId},credential())).status).toBe(404);
  });
  it('rejects missing credentials and oversized bodies', async () => {
    expect((await call('/v1/devices')).status).toBe(401);
    expect((await call('/v1/trial',{credential:'x'.repeat(40000)})).status).toBe(413);
  });
  it('rate limits anonymous trial creation', async () => {
    for(let i=0;i<5;i++) await trial();
    expect((await call('/v1/trial',{credential:credential('b')})).status).toBe(429);
  });
});

describe('purchase permissions', () => {
  it('recovers a missing subscription webhook through the exact order item lookup', async () => {
    billing({mode:'subscription'}); const providerFetch=globalThis.fetch;
    vi.stubGlobal('fetch', async (...args: Parameters<typeof fetch>) => {
      const url=new URL(String(args[0]));
      const subscription={type:'subscriptions',id:'40',attributes:{store_id:1,customer_id:7,order_item_id:30,
        product_id:4,variant_id:6,test_mode:true,status:'active',ends_at:null}};
      if(url.pathname==='/v1/subscriptions') {
        expect(url.searchParams.get('filter[order_item_id]')).toBe('30');
        return Response.json({data:[subscription]});
      }
      if(url.pathname==='/v1/subscriptions/40') return Response.json({data:subscription});
      return providerFetch(...args);
    });
    const response=await call('/v1/activate',{licenseKey:'secret'},credential());
    expect(response.status).toBe(200);
    const payload=(await jwtVerify((await response.json() as {lease:string}).lease,publicKey)).payload;
    expect(payload.mode).toBe('subscription');
    expect(payload.plan).toBe('studio-monthly');
  });
  it('applies a reduced device allowance when issuing new leases', async () => {
    billing();
    for(const token of ['a','b']) expect((await call('/v1/activate',{licenseKey:'secret'},credential(token))).status).toBe(200);
    await env.DB.prepare('UPDATE licenses SET device_limit=1').run();
    const devices=(await env.DB.prepare('SELECT * FROM installations ORDER BY created_at,id').all<Installation>()).results;
    await expect(issueLease(service,devices[0])).resolves.toBeTypeOf('string');
    await expect(issueLease(service,devices[1])).rejects.toThrow('device_limit');
  });
  it('cannot overwrite a later refund with an older in-flight provider response', async () => {
    billing(); const initialFetch=globalThis.fetch;
    let release!: () => void, reached!: () => void;
    const pending=new Promise<void>(resolve=>{release=resolve;});
    const started=new Promise<void>(resolve=>{reached=resolve;});
    let first=true;
    vi.stubGlobal('fetch',async (...args: Parameters<typeof fetch>) => {
      const response=await initialFetch(...args);
      if(first) {first=false; reached(); await pending;}
      return response;
    });
    const older=syncLicense(service,'10'); await started;
    billing({refunded:true}); await syncLicense(service,'10');
    vi.stubGlobal('fetch',initialFetch); release(); await older;
    expect((await env.DB.prepare('SELECT status FROM licenses').first()).status).toBe('disabled');
  });
  it('activates a new paid installation without requiring a guest trial first', async () => {
    billing();
    const response=await call('/v1/activate',{licenseKey:'secret-license'},credential());
    expect(response.status).toBe(200);
    expect((await env.DB.prepare('SELECT license_id FROM installations').first()).license_id).toBe('10');
  });
  it('links a verified purchase to the provider customer without storing its key', async () => {
    await trial(); billing();
    const response=await call('/v1/activate',{licenseKey:'secret-license'},credential());
    expect(response.status).toBe(200);
    const data=await response.json() as {lease:string};
    const {payload}=await jwtVerify(data.lease,publicKey);
    expect(payload.sub).toBe('lemon:1:7'); expect(payload.major).toBe(1); expect(payload.mode).toBe('perpetual');
    expect(JSON.stringify((await env.DB.prepare('SELECT * FROM licenses').all()).results)).not.toContain('secret-license');
  });
  it('rejects a purchase from the wrong store or environment', async () => {
    await trial(); billing({store:2});
    expect((await call('/v1/activate',{licenseKey:'secret'},credential())).status).toBe(403);
    billing({testMode:false});
    expect((await call('/v1/activate',{licenseKey:'secret'},credential())).status).toBe(403);
  });
  it('rejects refunded purchases and does not consume their device slot', async () => {
    await trial(); billing({refunded:true});
    expect((await call('/v1/activate',{licenseKey:'secret'},credential())).status).toBe(403);
    expect((await env.DB.prepare('SELECT license_id FROM installations').first()).license_id).toBeNull();
  });
  it('enforces a two-device limit even with concurrent activation', async () => {
    for(const token of ['a','b','c']) await trial(token);
    billing();
    const results=await Promise.all(['a','b','c'].map(token=>call('/v1/activate',{licenseKey:'secret'},credential(token))));
    expect(results.map(r=>r.status).sort()).toEqual([200,200,409]);
  });
  it('keeps staff revocation when provider data refreshes', async () => {
    await trial(); billing(); await call('/v1/activate',{licenseKey:'secret'},credential());
    await env.DB.prepare('UPDATE licenses SET blocked=1').run();
    expect((await call('/v1/refresh',{version:'1.0.0',platform:'mac-arm'},credential())).status).toBe(403);
  });
  it('does not grant subscriptions without a verified subscription snapshot', async () => {
    await trial(); billing({mode:'subscription'});
    expect((await call('/v1/activate',{licenseKey:'secret'},credential())).status).toBe(403);
  });
  it('honors paid cancellation until its end, never beyond', async () => {
    await trial(); billing(); await call('/v1/activate',{licenseKey:'secret'},credential());
    await env.DB.prepare("UPDATE licenses SET mode='subscription'").run();
    await env.DB.prepare('INSERT INTO subscriptions(id,order_item_id,variant_id,status,ends_at,checked_at) VALUES(?,?,?,?,?,?)').bind('40','30','6','cancelled',now()+500,now()).run();
    const device=await env.DB.prepare('SELECT * FROM installations').first<Installation>();
    const {payload}=await jwtVerify(await issueLease(service,device!),publicKey);
    expect(payload.exp).toBeLessThanOrEqual(now()+500);
    await env.DB.prepare("UPDATE subscriptions SET ends_at=?").bind(now()-1).run();
    await expect(issueLease(service,device!)).rejects.toThrow('access_expired');
  });
  it('never renews a stale snapshot beyond its original offline allowance', async () => {
    await trial(); billing(); await call('/v1/activate',{licenseKey:'secret'},credential());
    await env.DB.prepare('UPDATE licenses SET checked_at=?').bind(now()-8*DAY).run();
    const device=await env.DB.prepare('SELECT * FROM installations').first<Installation>();
    await expect(issueLease(service,device!)).rejects.toThrow('access_expired');
  });
});

describe('webhooks and staff', () => {
  async function staff(role = 'support', registered = true) {
    const pair=await generateKeyPair('RS256', {extractable:true});
    const issuer='https://looksy.cloudflareaccess.com';
    const key={...await exportJWK(pair.publicKey),kid:'staff-key',alg:'RS256',use:'sig'};
    vi.stubGlobal('fetch',vi.fn(async (url: URL | string) => {
      if (String(url)!==issuer+'/cdn-cgi/access/certs') throw new Error('Unexpected authority');
      return Response.json({keys:[key]});
    }));
    if(registered) await env.DB.prepare('INSERT INTO staff VALUES(?,?)').bind('staff-subject',role).run();
    const token=await new SignJWT({}).setProtectedHeader({alg:'RS256',kid:'staff-key'})
      .setIssuer(issuer).setAudience('staff-app').setSubject('staff-subject').setIssuedAt().setExpirationTime('5m').sign(pair.privateKey);
    return async (path: string, input?: unknown, origin = 'https://accounts.looksy.test') => worker.fetch(
      new Request('https://accounts.looksy.test/admin/api/'+path,{method:input?'POST':'GET',
        headers:{'Cf-Access-Jwt-Assertion':token,'Content-Type':'application/json','Origin':origin,'X-Looksy-Admin':'1'},
        body:input?JSON.stringify(input):undefined}),
      {...service,ACCESS_TEAM_DOMAIN:'looksy.cloudflareaccess.com',ACCESS_AUDIENCE:'staff-app'});
  }
  it('requires an explicit staff role even with a correctly signed Access identity', async () => {
    const admin=await staff('support',false);
    expect((await admin('customers')).status).toBe(403);
  });
  it('permits support trial extensions with an audit record but denies administrator actions', async () => {
    const device=await trial(); const admin=await staff();
    expect((await admin('customers')).status).toBe(200);
    expect((await admin('action',{action:'extend_trial',target:device.installationId,days:7,reason:'Support recovery'})).status).toBe(200);
    expect((await env.DB.prepare('SELECT actor,reason FROM audit').first())).toEqual({actor:'staff-subject',reason:'Support recovery'});
    expect((await admin('action',{action:'block_license',target:'10',reason:'Not permitted'})).status).toBe(403);
    expect((await admin('action',{action:'extend_trial',target:device.installationId,days:60,reason:'Too long'})).status).toBe(400);
  });
  it('rejects cross-origin staff mutations', async () => {
    const device=await trial(); const admin=await staff('admin');
    expect((await admin('action',{action:'revoke_device',target:device.installationId,reason:'Test'},'https://attacker.test')).status).toBe(403);
    expect((await env.DB.prepare('SELECT revoked FROM installations').first()).revoked).toBe(0);
  });
  it('rejects unsigned events', async () => expect((await call('/v1/webhooks/lemon',{})).status).toBe(401));
  it('processes signed events once and ignores their stale attributes', async () => {
    billing(); const event={meta:{event_name:'license_key_updated',test_mode:true},data:{id:'10',attributes:{status:'disabled'}}};
    const signature=hex(await crypto.subtle.sign('HMAC',await hmacKey(service.LEMON_WEBHOOK_SECRET),new TextEncoder().encode(JSON.stringify(event))));
    expect((await call('/v1/webhooks/lemon',event,undefined,{'X-Signature':signature})).status).toBe(200);
    const again=await call('/v1/webhooks/lemon',event,undefined,{'X-Signature':signature});
    expect(await again.json()).toEqual({duplicate:true});
    expect((await env.DB.prepare('SELECT status FROM licenses').first()).status).toBe('active');
  });
  it('does not treat a device key or a spoofed Access header as staff authority', async () => {
    await trial();
    const configured={...service,ACCESS_TEAM_DOMAIN:'looksy.cloudflareaccess.com',ACCESS_AUDIENCE:'staff-app'};
    const request=new Request('https://accounts.looksy.test/admin/api/customers',{headers:{Authorization:`Bearer ${credential()}`,'Cf-Access-Jwt-Assertion':'forged'}});
    expect((await worker.fetch(request,configured)).status).toBe(401);
  });
});
