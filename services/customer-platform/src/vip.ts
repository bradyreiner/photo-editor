import { ApiError, authenticate, body, DAY, hash, issueLease, json, now, str, type Installation, type ServiceEnv } from './core';
import { accountSummary, requireAccount } from './accounts';

type Actor = {subject:string;role:string};
type Vip = {id:string;plan:string;features:string;major:number;device_limit:number;redemption_limit:number;expires_at:number;access_ends_at:number;revoked:number};
const integer = (value:unknown,min:number,max:number) => {
  const n=Number(value); if(!Number.isInteger(n)||n<min||n>max) throw new ApiError(400,'invalid_vip_policy'); return n;
};
function code(): string {
  const bytes=crypto.getRandomValues(new Uint8Array(24));
  return 'LOOKSY-'+[...bytes].map(x=>x.toString(16).padStart(2,'0')).join('').toUpperCase();
}
function audit(env:ServiceEnv,actor:Actor,action:string,target:string,reason:string):D1PreparedStatement {
  return env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)').bind(crypto.randomUUID(),actor.subject,action,target,reason,now());
}
export async function adminVip(request:Request,env:ServiceEnv,actor:Actor,path:string):Promise<Response|null> {
  if(!path.startsWith('/admin/api/vip')) return null;
  if(actor.role!=='admin') throw new ApiError(403,'admin_required');
  if(request.method==='GET' && path==='/admin/api/vip') {
    const rows=await env.DB.prepare(`SELECT id,plan,features,major,device_limit,redemption_limit,expires_at,access_ends_at,revoked,created_at,
      (SELECT count(*) FROM vip_redemptions WHERE code_id=c.id) AS redemptions FROM vip_codes c ORDER BY created_at DESC,id LIMIT 100`).all();
    return json({codes:rows.results});
  }
  if(request.method!=='POST'||request.headers.get('Origin')!==new URL(request.url).origin||request.headers.get('X-Looksy-Admin')!=='1') throw new ApiError(403,'invalid_origin');
  const input=await body(request),reason=str(input.reason,500).trim();
  if(!reason) throw new ApiError(400,'reason_required');
  if(path==='/admin/api/vip/revoke') {
    const codeId=str(input.id);
    const statements=await env.DB.batch([
      env.DB.prepare('UPDATE vip_codes SET revoked=1 WHERE id=?').bind(codeId),
      env.DB.prepare(`INSERT INTO audit SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM vip_codes WHERE id=?)`)
        .bind(crypto.randomUUID(),actor.subject,'vip_revoked',codeId,reason,now(),codeId),
    ]);
    if(!statements[0].meta.changes) throw new ApiError(404,'not_found');
    return json({ok:true});
  }
  let policy:Omit<Vip,'id'|'revoked'>,replaced:string|undefined;
  if(path==='/admin/api/vip/reissue') {
    replaced=str(input.id);
    const previous=await env.DB.prepare('SELECT * FROM vip_codes WHERE id=?').bind(replaced).first<Vip>();
    if(!previous) throw new ApiError(404,'not_found');
    if(previous.expires_at<=now()||previous.access_ends_at<=now()) throw new ApiError(400,'vip_expired_create_new');
    policy=previous;
  } else if(path==='/admin/api/vip/create') {
    const features=input.features;
    if(!Array.isArray(features)||!features.length||!features.every(x=>['editor','agent','beta'].includes(x))||new Set(features).size!==features.length) throw new ApiError(400,'invalid_vip_policy');
    policy={plan:str(input.plan,40),features:JSON.stringify(features),major:integer(input.major,0,1000),
      device_limit:integer(input.deviceLimit,1,100),redemption_limit:integer(input.redemptionLimit,1,1000),
      expires_at:integer(input.expiresAt,now()+60,now()+3660*DAY),access_ends_at:integer(input.accessEndsAt,now()+60,now()+3660*DAY)};
  } else throw new ApiError(404,'not_found');
  const raw=code(),codeId=crypto.randomUUID();
  const statements:D1PreparedStatement[]=[];
  if(replaced) statements.push(env.DB.prepare('UPDATE vip_codes SET revoked=1 WHERE id=?').bind(replaced),audit(env,actor,'vip_reissued',replaced,reason));
  statements.push(env.DB.prepare(`INSERT INTO vip_codes(id,code_hash,plan,features,major,device_limit,redemption_limit,expires_at,access_ends_at,created_at)
    VALUES(?,?,?,?,?,?,?,?,?,?)`).bind(codeId,await hash(raw),policy.plan,policy.features,policy.major,policy.device_limit,policy.redemption_limit,policy.expires_at,policy.access_ends_at,now()),
    audit(env,actor,'vip_created',codeId,reason));
  await env.DB.batch(statements);
  // Raw code is returned once, never persisted or included in an audit record.
  return json({id:codeId,code:raw,expiresAt:policy.expires_at,accessEndsAt:policy.access_ends_at},201);
}
export async function redeemVip(request:Request,env:ServiceEnv,device:Installation):Promise<Response> {
  const account=await requireAccount(env,device),input=await body(request),raw=str(input.code,80).trim().toUpperCase();
  if(!/^LOOKSY-[0-9A-F]{48}$/.test(raw)) throw new ApiError(403,'vip_unavailable');
  const grant=await env.DB.prepare('SELECT * FROM vip_codes WHERE code_hash=?').bind(await hash(raw)).first<Vip>();
  if(!grant||grant.revoked||grant.expires_at<=now()||grant.access_ends_at<=now()) throw new ApiError(403,'vip_unavailable');
  let redemption=await env.DB.prepare('SELECT license_id FROM vip_redemptions WHERE code_id=? AND account_id=?')
    .bind(grant.id,account.id).first<{license_id:string}>();
  if(!redemption) {
    const licenseId=`vip:${crypto.randomUUID()}`,time=now();
    try {
      await env.DB.batch([
        env.DB.prepare('INSERT OR IGNORE INTO customers(id,email,created_at) VALUES(?,?,?)').bind(account.id,account.email,time),
        env.DB.prepare(`INSERT INTO licenses(id,customer_id,order_id,order_item_id,variant_id,plan,mode,major,features,device_limit,status,expires_at,checked_at)
          VALUES(?,?,?,?,'vip',?,'perpetual',?,?,?,'active',?,?)`).bind(licenseId,account.id,grant.id,licenseId,grant.plan,grant.major,grant.features,grant.device_limit,grant.access_ends_at,time),
        env.DB.prepare('INSERT INTO vip_redemptions(code_id,account_id,license_id,redeemed_at) VALUES(?,?,?,?)').bind(grant.id,account.id,licenseId,time),
        env.DB.prepare('INSERT INTO account_licenses VALUES(?,?,?)').bind(licenseId,account.id,time),
        env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)').bind(crypto.randomUUID(),account.id,'vip_redeemed',grant.id,'Customer redeemed complimentary access',time),
      ]);
      redemption={license_id:licenseId};
    } catch(error) {
      // An identical concurrent redemption may already have won; it is idempotent.
      redemption=await env.DB.prepare('SELECT license_id FROM vip_redemptions WHERE code_id=? AND account_id=?').bind(grant.id,account.id).first<{license_id:string}>();
      if(!redemption) {
        if(String(error).includes('vip_unavailable')) throw new ApiError(403,'vip_unavailable');
        throw error;
      }
    }
  }
  await issueLease(env,{...device,customer_id:account.id,license_id:redemption.license_id});
  try { await env.DB.prepare('UPDATE installations SET customer_id=?,license_id=? WHERE id=? AND revoked=0').bind(account.id,redemption.license_id,device.id).run(); }
  catch(error) { if(String(error).includes('device_limit')) throw new ApiError(409,'device_limit'); throw error; }
  const current=await authenticate(request,env);
  return json({lease:await issueLease(env,current),...await accountSummary(env,current)});
}
