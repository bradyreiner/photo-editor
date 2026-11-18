import { ApiError, body, json, now, str, type ServiceEnv } from './core';

type Actor={subject:string;role:string};
export function accountDeletionStatements(env:ServiceEnv,id:string,email:string):D1PreparedStatement[] {
  const time=now();
  return [
    env.DB.prepare('UPDATE accounts SET disabled=1,email=? WHERE id=?').bind(`deleted-${crypto.randomUUID()}@deleted.invalid`,id),
    env.DB.prepare('DELETE FROM email_challenges WHERE email=? OR installation_id IN(SELECT id FROM installations WHERE account_id=?)').bind(email,id),
    env.DB.prepare('DELETE FROM paddle_accounts WHERE account_id=?').bind(id),
    env.DB.prepare('UPDATE paddle_checkouts SET fulfilled_at=? WHERE account_id=? AND fulfilled_at IS NULL').bind(time,id),
    env.DB.prepare(`INSERT OR IGNORE INTO privacy_tombstones(customer_id,provider_customer_hash,deleted_at)
      SELECT DISTINCT customer_id,NULL,? FROM licenses WHERE id IN(SELECT license_id FROM account_licenses WHERE account_id=?)`).bind(time,id),
    env.DB.prepare('UPDATE customers SET email=NULL,provider_customer_id=NULL WHERE id=? OR id IN(SELECT customer_id FROM licenses WHERE id IN(SELECT license_id FROM account_licenses WHERE account_id=?))').bind(id,id),
    env.DB.prepare("UPDATE licenses SET blocked=1,status='deleted' WHERE id IN(SELECT license_id FROM account_licenses WHERE account_id=?)").bind(id),
    env.DB.prepare("UPDATE installations SET revoked=1,app_version='',platform='' WHERE account_id=?").bind(id),
  ];
}
export async function adminAccounts(request:Request,env:ServiceEnv,actor:Actor,path:string):Promise<Response|null> {
  if(request.method==='GET' && path==='/admin/api/accounts') {
    const query=(new URL(request.url).searchParams.get('q')||'').slice(0,254).trim().toLowerCase();
    const rows=await env.DB.prepare(`SELECT a.id,a.email,a.created_at,a.disabled,a.trial_ends_at,
      (SELECT count(*) FROM installations WHERE account_id=a.id AND revoked=0) AS active_devices
      FROM accounts a WHERE ?='' OR a.id=? OR a.email=? ORDER BY a.created_at DESC,a.id LIMIT 100`).bind(query,query,query).all();
    return json({accounts:rows.results,role:actor.role});
  }
  if(request.method==='GET' && path==='/admin/api/account') {
    const id=str(new URL(request.url).searchParams.get('id'));
    const account=await env.DB.prepare('SELECT id,email,created_at,disabled,trial_ends_at FROM accounts WHERE id=?').bind(id).first();
    if(!account) throw new ApiError(404,'not_found');
    const [licenses,devices,audit]=await Promise.all([
      env.DB.prepare('SELECT l.* FROM licenses l JOIN account_licenses a ON a.license_id=l.id WHERE a.account_id=? LIMIT 100').bind(id).all(),
      env.DB.prepare('SELECT id,license_id,trial_ends_at,revoked,last_seen_at,app_version,platform FROM installations WHERE account_id=? LIMIT 100').bind(id).all(),
      env.DB.prepare('SELECT actor,action,target,reason,created_at FROM audit WHERE target=? OR actor=? ORDER BY created_at DESC LIMIT 100').bind(id,id).all(),
    ]);
    return json({account,licenses:licenses.results,devices:devices.results,audit:audit.results});
  }
  if(request.method==='POST' && path==='/admin/api/account/action') {
    if(actor.role!=='admin') throw new ApiError(403,'admin_required');
    if(request.headers.get('Origin')!==new URL(request.url).origin||request.headers.get('X-Looksy-Admin')!=='1') throw new ApiError(403,'invalid_origin');
    const input=await body(request),id=str(input.id),action=str(input.action),reason=str(input.reason,500).trim();
    if(!['revoke_sessions','disable','enable','delete'].includes(action)||!reason) throw new ApiError(400,'invalid_account_action');
    const account=await env.DB.prepare('SELECT disabled,email FROM accounts WHERE id=?').bind(id).first<{disabled:number;email:string}>();
    if(!account) throw new ApiError(404,'not_found');
    const statements:D1PreparedStatement[]=[];
    if(action==='disable'||action==='enable') statements.push(env.DB.prepare('UPDATE accounts SET disabled=? WHERE id=?').bind(action==='disable'?1:0,id));
    if(action==='delete') statements.push(...accountDeletionStatements(env,id,account.email));
    if(action!=='enable') statements.push(env.DB.prepare('UPDATE installations SET revoked=1 WHERE account_id=?').bind(id));
    statements.push(env.DB.prepare('INSERT INTO audit VALUES(?,?,?,?,?,?)').bind(crypto.randomUUID(),actor.subject,`account_${action}`,id,reason,now()));
    await env.DB.batch(statements);
    return json({ok:true});
  }
  return null;
}
