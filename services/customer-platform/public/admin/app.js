const $ = id => document.getElementById(id);
let after = '', role = '', selected = '', pending, selectedAccount=false;
const date = value => value ? new Date(value * 1000).toLocaleString() : 'Not reported';
async function api(path, input) {
  const response = await fetch('/admin/api/' + path, input ? {
    method: 'POST', headers: {'Content-Type':'application/json','X-Looksy-Admin':'1'}, body: JSON.stringify(input),
  } : {cache:'no-store'});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'Request failed');
  return data;
}
function row(parent, title, detail, buttons = []) {
  const container = document.createElement('div'); container.className = 'row';
  const copy = document.createElement('div'); copy.className = 'copy';
  const label = document.createElement('strong'); label.textContent = title;
  const secondary = document.createElement('div'); secondary.className = 'muted'; secondary.textContent = detail;
  copy.append(label, secondary); container.append(copy);
  for (const [text, handler] of buttons) { const button = document.createElement('button'); button.type='button'; button.className='secondary'; button.textContent=text; button.onclick=handler; container.append(button); }
  parent.append(container);
}
function ask(action, target, title, days) {
  pending={action,target,days}; $('action-title').textContent=title; $('reason').value=''; $('action-error').textContent=''; $('action-dialog').showModal();
}
async function details(customer, isAccount=false) {
  try {
    selected=customer; selectedAccount=isAccount; const data=await api((isAccount?'account':'customer')+'?id='+encodeURIComponent(customer));
    $('detail').hidden=false; $('customer-name').textContent=customer;
    $('privacy-actions').replaceChildren();
    if (isAccount && role==='admin') {
      for(const [action,label] of [['revoke_sessions','Sign out all devices'],[data.account.disabled?'enable':'disable',data.account.disabled?'Enable account':'Disable account'],['delete','Delete account personal data']]) {
        const button=document.createElement('button');button.type='button';button.textContent=label;button.onclick=async()=>{const reason=prompt(label+': reason?');if(!reason?.trim())return;try{await api('account/action',{id:customer,action,reason:reason.trim()});await details(customer,true);}catch(error){$('status').textContent=error.message;}};$('privacy-actions').append(button);
      }
    }
    if (!isAccount && (role === 'admin' || role === 'support')) {
      const exportButton=document.createElement('button'); exportButton.type='button'; exportButton.className='secondary'; exportButton.textContent='Export customer data';
      exportButton.onclick=async()=>{const reason=prompt('Reason for exporting this customer data?');if(!reason?.trim())return;try{const result=await api('customer-export',{customerId:customer,reason:reason.trim()});const blob=new Blob([JSON.stringify(result.export,null,2)],{type:'application/json'});const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download='looksy-customer-export.json';link.click();URL.revokeObjectURL(link.href);await details(customer);}catch(error){$('status').textContent=error.message;}};
      $('privacy-actions').append(exportButton);
      if(role==='admin') {
        const deleteButton=document.createElement('button'); deleteButton.type='button'; deleteButton.textContent='Delete personal data';
        deleteButton.onclick=async()=>{if(!confirm('Erase contact details and disable this customer’s local licenses and devices? Historical revocations and audit records are retained.'))return;const reason=prompt('Reason for deleting personal data?');if(!reason?.trim())return;try{await api('customer-delete',{customerId:customer,reason:reason.trim()});await details(customer);}catch(error){$('status').textContent=error.message;}};
        $('privacy-actions').append(deleteButton);
      }
    }
    for (const id of ['licenses','devices','audit']) $(id).replaceChildren();
    for (const license of data.licenses) row($('licenses'), `${license.plan} · ${license.mode}`, `License ${license.id} · ${license.blocked ? 'Blocked' : license.status} · ${license.device_limit} devices · ${license.expires_at ? 'expires '+date(license.expires_at) : 'No fixed license expiry'}`,
      role==='admin' ? [[license.blocked ? 'Restore access' : 'Block access',()=>ask(license.blocked?'unblock_license':'block_license',license.id,'Change license access')]] : []);
    for (const device of data.devices) row($('devices'), `${device.platform || 'Guest device'} · ${device.app_version || 'Version not reported'}`, `${device.id} · ${device.revoked ? 'Revoked' : 'Active'} · last seen ${date(device.last_seen_at)}`,
      device.revoked ? [] : [['Revoke',()=>ask('revoke_device',device.id,'Revoke this device')],...(!device.license_id ? [['Extend trial 14 days',()=>ask('extend_trial',device.id,'Extend trial by 14 days',14)]] : [])]);
    for (const event of data.audit) row($('audit'), event.action, `${date(event.created_at)} · ${event.actor} · ${event.reason}`);
    if (!data.audit.length) row($('audit'), 'No support actions recorded', 'Changes made here will appear in this history.');
    $('status').textContent='Customer loaded. Offline access lasts until the issued lease expires.';
  } catch(error) { $('status').textContent=error.message; }
}
async function list() {
  $('status').textContent='Loading customers…';
  try {
    const data=await api('customers?q='+encodeURIComponent($('query').value.trim())+'&after='+encodeURIComponent(after));
    role=data.role; after=data.next||''; $('next').hidden=!after; $('customers').replaceChildren();
    if(role==='admin'||role==='support') { await loadOperations(); await loadAccounts(); }
    if(role==='admin') await loadVip();
    for(const customer of data.customers) row($('customers'),customer.email||'Guest trial', `${customer.id} · last seen ${date(customer.last_seen_at)}`, [['Open',()=>details(customer.id)]]);
    $('status').textContent=data.customers.length ? `${data.customers.length} customers` : 'No customers found.';
  } catch(error) { $('status').textContent=error.message; }
}
async function loadOperations() {
  try {
    const data=await api('operations'); $('ops').hidden=false;
    const w=data.webhooks, l=data.licenses;
    $('ops-summary').textContent=`${w.pending} pending webhooks · ${w.failed} failed at least once · ${w.due} ready to retry · ${l.stale} stale license checks · ${data.checkouts?.pending??0} unfulfilled checkouts (includes unpaid) · backup status not connected`;
    if(role!=='admin') { $('webhooks').replaceChildren(); row($('webhooks'),'Webhook detail access is admin only',''); return; }
    const queue=await api('webhooks'); $('webhooks').replaceChildren();
    for(const item of queue.webhooks) row($('webhooks'),`${item.event} · ${item.attempts}/8 attempts`,`${item.digest} · next retry ${date(item.next_retry_at)} · ${item.last_error_code||'waiting'}`,
      [[item.attempts>=8?'Retry manually':'Retry now',async()=>{const reason=prompt('Reason for manually retrying this webhook?');if(!reason?.trim())return;try{await api('webhook-retry',{digest:item.digest,reason:reason.trim()});await loadOperations();}catch(error){$('status').textContent=error.message;}}]]);
    if(!queue.webhooks.length) row($('webhooks'),'No pending webhooks','New or failed provider events will appear here.');
  } catch(error) { $('ops-summary').textContent=error.message; }
}
$('search').onsubmit=event=>{event.preventDefault();after='';void list();};
$('next').onclick=()=>void list();
$('cancel').onclick=()=> $('action-dialog').close();
$('action-form').onsubmit=async event=>{event.preventDefault(); const buttons=$('action-form').querySelectorAll('button'); buttons.forEach(b=>b.disabled=true); try {await api('action',{...pending,reason:$('reason').value.trim()});$('action-dialog').close();await details(selected,selectedAccount);} catch(error){$('action-error').textContent=error.message;} finally {buttons.forEach(b=>b.disabled=false);} };
void list();

async function loadAccounts() {
  const data=await api('accounts?q='+encodeURIComponent($('query').value.trim()));$('accounts').replaceChildren();
  for(const account of data.accounts) row($('accounts'),account.email,`${account.id} · ${account.disabled?'Disabled':account.active_devices+' active devices'}`,[['Open',()=>details(account.id,true)]]);
  if(!data.accounts.length) row($('accounts'),'No verified accounts found','');
}
async function loadVip() {
  const data=await api('vip');$('vip-panel').hidden=false;$('vip-codes').replaceChildren();
  for(const code of data.codes) row($('vip-codes'),code.plan,`${code.id} · ${code.revoked?'Revoked':code.redemptions+'/'+code.redemption_limit+' redeemed'} · major ${code.major} · ${code.device_limit} devices · redeem by ${date(code.expires_at)} · access ends ${date(code.access_ends_at)}`,
    code.revoked?[]:[['Revoke',()=>changeVip('revoke',code.id)],['Reissue',()=>changeVip('reissue',code.id)]]);
}
async function changeVip(action,id) {
  const reason=prompt(action==='reissue'?'Reissue revokes the old code and its access. Reason?':'Revoke this code and redeemed access? Reason?');if(!reason?.trim())return;
  try {const result=await api('vip/'+action,{id,reason:reason.trim()});$('vip-result').textContent=result.code?'Copy this code now; it is shown once: '+result.code:'Code revoked. Existing offline leases last until their signed expiry.';await loadVip();}catch(error){$('vip-result').textContent=error.message;}
}
$('create-vip').onclick=()=>{$('vip-error').textContent='';$('vip-result').textContent='';$('vip-dialog').showModal();};
$('vip-cancel').onclick=()=> $('vip-dialog').close();
$('vip-form').onsubmit=async event=>{event.preventDefault();const form=$('vip-form'),button=form.querySelector('button[type="submit"]')||form.querySelector('button:last-child');button.disabled=true;try{const input=Object.fromEntries(new FormData(form));for(const name of ['major','deviceLimit','redemptionLimit'])input[name]=Number(input[name]);for(const name of ['expiresAt','accessEndsAt'])input[name]=Math.floor(new Date(input[name]).getTime()/1000);input.features=input.features.split(',').map(x=>x.trim()).filter(Boolean);const result=await api('vip/create',input);$('vip-result').textContent='Copy this code now; it is shown once: '+result.code;$('vip-dialog').close();await loadVip();}catch(error){$('vip-error').textContent=error.message;}finally{button.disabled=false;}};
