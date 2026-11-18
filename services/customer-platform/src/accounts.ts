import { syncLicense } from './billing';
import { syncPaddleLicense } from './paddle';
import { ApiError, authenticate, body, boundedInt, DAY, hash, hex, hmacKey, issueLease, json, now, rateLimit, str, type Installation, type ServiceEnv } from './core';

type Account = { id: string; email: string; trial_ends_at: number; disabled: number };
export function bearer(request: Request): string {
  const token = request.headers.get('Authorization')?.replace(/^Bearer /, '');
  if (!token || !/^[A-Za-z0-9_-]{43}$/.test(token)) throw new ApiError(401, 'authentication_required');
  return token;
}
export async function registerInstallation(env: ServiceEnv, credential: string): Promise<Installation> {
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
async function secretDigest(env: ServiceEnv, value: string): Promise<string> {
  return hex(await crypto.subtle.sign('HMAC', await hmacKey(env.RATE_LIMIT_SECRET), new TextEncoder().encode(value)));
}
async function account(env: ServiceEnv, device: Installation): Promise<Account | null> {
  if (!device.account_id) return null;
  const value = await env.DB.prepare('SELECT id,email,trial_ends_at,disabled FROM accounts WHERE id=?').bind(device.account_id).first<Account>();
  if (!value || value.disabled) throw new ApiError(401, 'account_disabled');
  return value;
}
export async function accountSummary(env: ServiceEnv, device: Installation): Promise<Record<string, unknown>> {
  const value = await account(env, device);
  const licenses = value ? await env.DB.prepare(`SELECT l.id,l.plan,l.mode,l.major,CASE WHEN EXISTS(SELECT 1 FROM vip_redemptions r JOIN vip_codes c ON c.id=r.code_id WHERE r.license_id=l.id AND (c.revoked=1 OR c.access_ends_at<=unixepoch())) THEN 'revoked' ELSE l.status END AS status,l.blocked,l.expires_at,l.device_limit
    FROM licenses l JOIN account_licenses a ON a.license_id=l.id WHERE a.account_id=? ORDER BY a.linked_at DESC LIMIT 100`).bind(value.id).all() : {results:[]};
  return {account: value ? {id:value.id,email:value.email} : null, licenses:licenses.results, installationId:device.id};
}
export async function requireAccount(env: ServiceEnv, device: Installation): Promise<Account> {
  const value = await account(env, device);
  if (!value) throw new ApiError(401, 'account_sign_in_required');
  return value;
}
// A validated merchant key or an already activated installation proves ownership.
// A verified email address alone never links purchases.
export async function linkLicense(env: ServiceEnv, device: Installation, licenseId: string): Promise<void> {
  const owner = await env.DB.prepare('SELECT account_id FROM account_licenses WHERE license_id=?').bind(licenseId).first<{account_id:string}>();
  if (owner && owner.account_id!==device.account_id) throw new ApiError(409, 'license_already_linked');
  if (!device.account_id) return;
  try {
    await env.DB.prepare('INSERT OR IGNORE INTO account_licenses(license_id,account_id,linked_at) VALUES(?,?,?)')
      .bind(licenseId, device.account_id, now()).run();
  } catch (error) {
    if (String(error).includes('license_already_linked')) throw new ApiError(409, 'license_already_linked');
    throw error;
  }
}
async function emailRequest(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request, env, 'email_request', 5);
  const input = await body(request), email = str(input.email, 254).trim().toLowerCase();
  if (!/^[a-z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?\.[a-z]{2,63}$/.test(email)) throw new ApiError(400, 'invalid_email');
  if (!env.EMAIL_FROM || env.EMAIL_FROM.includes('.invalid') || !env.RESEND_API_KEY) throw new ApiError(503, 'email_not_configured');
  await registerInstallation(env, bearer(request));
  const device = await authenticate(request, env, true);
  const current = await account(env, device);
  if (current && current.email !== email) throw new ApiError(409, 'sign_out_required');
  const time = now(), window = Math.floor(time / 900);
  for (const identity of [`email:${await secretDigest(env,email)}`,`installation:${device.id}`]) {
    const row = await env.DB.prepare(`INSERT INTO rate_buckets(id,count,expires_at) VALUES(?,1,?)
      ON CONFLICT(id) DO UPDATE SET count=count+1 RETURNING count`).bind(`email_auth:${window}:${identity}`,time+900).first<{count:number}>();
    if (!row || row.count>3) throw new ApiError(429,'rate_limited');
  }
  const challengeId = crypto.randomUUID();
  // Rejection sampling keeps all eight-digit codes equally likely.
  const bytes = new Uint32Array(1); do { crypto.getRandomValues(bytes); } while(bytes[0]>=4200000000);
  const code = String(bytes[0]%100000000).padStart(8,'0'), expiresAt = time+600;
  await env.DB.batch([
    env.DB.prepare('UPDATE email_challenges SET consumed_at=? WHERE installation_id=? AND consumed_at IS NULL').bind(time,device.id),
    env.DB.prepare('INSERT INTO email_challenges(id,installation_id,email,code_hash,expires_at,created_at) VALUES(?,?,?,?,?,?)')
      .bind(challengeId,device.id,email,await secretDigest(env,`${challengeId}:${code}`),expiresAt,time),
  ]);
  let response: Response;
  try {
    response = await fetch('https://api.resend.com/emails', {method:'POST',redirect:'error',signal:AbortSignal.timeout(10000),
      headers:{Authorization:`Bearer ${env.RESEND_API_KEY}`,'Content-Type':'application/json','Idempotency-Key':challengeId},
      body:JSON.stringify({from:env.EMAIL_FROM,to:[email],subject:'Your Looksy sign-in code',
        text:`Your Looksy sign-in code is ${code}. It expires in 10 minutes and works only in the app that requested it. If you did not request this, ignore this email.`})});
  } catch {
    await env.DB.prepare('UPDATE email_challenges SET consumed_at=? WHERE id=?').bind(time,challengeId).run();
    throw new ApiError(502,'email_unavailable');
  }
  await response.body?.cancel();
  if (!response.ok) {
    await env.DB.prepare('UPDATE email_challenges SET consumed_at=? WHERE id=?').bind(time,challengeId).run();
    throw new ApiError(502,'email_unavailable');
  }
  return json({challengeId,expiresAt});
}
async function emailVerify(request: Request, env: ServiceEnv): Promise<Response> {
  await rateLimit(request, env, 'email_verify', 20);
  const device = await authenticate(request, env, true), input = await body(request);
  const challengeId = str(input.challengeId,100), code = str(input.code,8), time = now();
  if (!/^\d{8}$/.test(code)) throw new ApiError(400,'invalid_code');
  const challenge = await env.DB.prepare(`UPDATE email_challenges SET attempts=attempts+1
    WHERE id=? AND installation_id=? AND consumed_at IS NULL AND expires_at>? AND attempts<5
    RETURNING email,code_hash`).bind(challengeId,device.id,time).first<{email:string;code_hash:string}>();
  if (!challenge) throw new ApiError(403,'invalid_or_expired_code');
  const signature = Uint8Array.from(challenge.code_hash.match(/../g)!,value=>parseInt(value,16));
  if (!await crypto.subtle.verify('HMAC',await hmacKey(env.RATE_LIMIT_SECRET),signature,new TextEncoder().encode(`${challengeId}:${code}`))) throw new ApiError(403,'invalid_or_expired_code');
  const current = await account(env, device);
  if (current && current.email!==challenge.email) throw new ApiError(409,'sign_out_required');
  const consumed = await env.DB.prepare(`UPDATE email_challenges SET consumed_at=?
    WHERE id=? AND installation_id=? AND consumed_at IS NULL RETURNING id`).bind(time,challengeId,device.id).first();
  if (!consumed) throw new ApiError(403,'invalid_or_expired_code');
  // The minimum persists when reinstalling/signing back in, including expired trials.
  await env.DB.prepare(`INSERT INTO accounts(id,email,trial_ends_at,created_at) VALUES(?,?,?,?)
    ON CONFLICT(email) DO UPDATE SET trial_ends_at=min(accounts.trial_ends_at,excluded.trial_ends_at)`)
    .bind(`account:${crypto.randomUUID()}`,challenge.email,device.trial_ends_at,time).run();
  const value = await env.DB.prepare('SELECT * FROM accounts WHERE email=?').bind(challenge.email).first<Account>();
  if (!value || value.disabled) throw new ApiError(401,'account_disabled');
  const statements:D1PreparedStatement[]=[env.DB.prepare(`UPDATE installations SET account_id=?,trial_ends_at=min(trial_ends_at,?),account_session_expires_at=?
    WHERE id=? AND revoked=0 AND (account_id IS NULL OR account_id=?)`).bind(value.id,value.trial_ends_at,time+30*DAY,device.id,value.id)];
  if(device.license_id) statements.push(env.DB.prepare(`INSERT OR IGNORE INTO account_licenses(license_id,account_id,linked_at)
    SELECT ?,account_id,? FROM installations WHERE id=? AND revoked=0 AND account_id=?`).bind(device.license_id,time,device.id,value.id));
  statements.push(env.DB.prepare(`INSERT INTO audit SELECT ?,?,?,?,?,? WHERE EXISTS
    (SELECT 1 FROM installations WHERE id=? AND account_id=? AND revoked=0)`)
    .bind(crypto.randomUUID(),value.id,'account_signed_in',value.id,'Verified email code',time,device.id,value.id));
  let attached:D1Result[];
  try { attached=await env.DB.batch(statements); }
  catch(error) { if(String(error).includes('license_already_linked')) throw new ApiError(409,'license_already_linked');throw error; }
  if(!attached[0].meta.changes) throw new ApiError(409,'sign_out_required');
  const updated = await authenticate(request,env);
  let lease: string | undefined, accessError: string | undefined;
  try { lease=await issueLease(env,updated); }
  catch(error) { if(error instanceof ApiError && error.status===403) accessError=error.code; else throw error; }
  return json({...await accountSummary(env,updated),lease,accessError});
}
export async function accountRoute(request: Request, env: ServiceEnv, path: string): Promise<Response | null> {
  if(request.method==='POST' && path==='/v1/account/email/request') return emailRequest(request,env);
  if(request.method==='POST' && path==='/v1/account/email/verify') return emailVerify(request,env);
  if(!['/v1/account','/v1/account/logout','/v1/account/license'].includes(path)) return null;
  const device=await authenticate(request,env,path==='/v1/account/logout');
  if(request.method==='GET' && path==='/v1/account') return json(await accountSummary(env,device));
  if(request.method==='POST' && path==='/v1/account/logout') {
    await env.DB.batch([
      env.DB.prepare('UPDATE installations SET revoked=1 WHERE id=?').bind(device.id),
      env.DB.prepare('UPDATE email_challenges SET consumed_at=? WHERE installation_id=? AND consumed_at IS NULL').bind(now(),device.id),
      env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)').bind(crypto.randomUUID(),device.account_id??device.id,'account_signed_out',device.account_id??device.id,'Customer signed out this device',now()),
    ]);
    return json({ok:true});
  }
  if(request.method==='POST' && path==='/v1/account/license') {
    const value=await requireAccount(env,device), input=await body(request), licenseId=str(input.licenseId);
    const license=await env.DB.prepare(`SELECT l.customer_id FROM licenses l JOIN account_licenses a ON a.license_id=l.id
      WHERE a.account_id=? AND l.id=?`).bind(value.id,licenseId).first<{customer_id:string}>();
    if(!license) throw new ApiError(404,'license_not_linked');
    if(!licenseId.startsWith('vip:')) {
      try { if(licenseId.startsWith('paddle:')) await syncPaddleLicense(env,licenseId);else await syncLicense(env,licenseId); }
      catch(error) { if(!(error instanceof ApiError)||error.status!==502) throw error; }
    }
    await issueLease(env,{...device,customer_id:license.customer_id,license_id:licenseId});
    try { await env.DB.prepare('UPDATE installations SET license_id=?,customer_id=? WHERE id=? AND revoked=0')
      .bind(licenseId,license.customer_id,device.id).run(); }
    catch(error) { if(String(error).includes('device_limit')) throw new ApiError(409,'device_limit'); throw error; }
    return json({lease:await issueLease(env,await authenticate(request,env)),...await accountSummary(env,device)});
  }
  throw new ApiError(404,'not_found');
}
