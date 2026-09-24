const $ = id => document.getElementById(id);
let after = '', role = '', selected = '', pending;
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
async function details(customer) {
  try {
    selected=customer; const data=await api('customer?id='+encodeURIComponent(customer));
    $('detail').hidden=false; $('customer-name').textContent=customer;
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
    for(const customer of data.customers) row($('customers'),customer.email||'Guest trial', `${customer.id} · last seen ${date(customer.last_seen_at)}`, [['Open',()=>details(customer.id)]]);
    $('status').textContent=data.customers.length ? `${data.customers.length} customers` : 'No customers found.';
  } catch(error) { $('status').textContent=error.message; }
}
$('search').onsubmit=event=>{event.preventDefault();after='';void list();};
$('next').onclick=()=>void list();
$('cancel').onclick=()=> $('action-dialog').close();
$('action-form').onsubmit=async event=>{event.preventDefault(); const buttons=$('action-form').querySelectorAll('button'); buttons.forEach(b=>b.disabled=true); try {await api('action',{...pending,reason:$('reason').value.trim()});$('action-dialog').close();await details(selected);} catch(error){$('action-error').textContent=error.message;} finally {buttons.forEach(b=>b.disabled=false);} };
void list();
