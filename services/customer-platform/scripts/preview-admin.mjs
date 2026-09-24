// Visual fixture only. Never deployed; no real customer data or production auth bypass.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
const root=new URL('../public/',import.meta.url);
const customer={id:'lemon:demo:7',email:'photographer@example.test',created_at:1790000000,last_seen_at:1790180000};
createServer(async(req,res)=>{
  const url=new URL(req.url,'http://127.0.0.1:8788');
  if(url.pathname==='/admin/api/customers') {res.setHeader('Content-Type','application/json');res.end(JSON.stringify({customers:[customer],next:null,role:'admin'}));return;}
  if(url.pathname==='/admin/api/customer') {res.setHeader('Content-Type','application/json');res.end(JSON.stringify({
    licenses:[{id:'10',plan:'Studio',mode:'perpetual',status:'active',blocked:0,device_limit:2,expires_at:null}],
    devices:[{id:'example-mac-device',platform:'mac-arm',app_version:'1.0.0',revoked:0,last_seen_at:1790180000,license_id:'10'}],
    audit:[]}));return;}
  const paths={'/admin/':'admin/index.html','/admin/app.js':'admin/app.js','/admin/style.css':'admin/style.css'};
  if(req.method!=='GET'||!paths[url.pathname]){res.writeHead(404);res.end();return;}
  const path=paths[url.pathname];res.setHeader('Content-Type',path.endsWith('.js')?'text/javascript':path.endsWith('.css')?'text/css':'text/html');
  try {res.end(await readFile(new URL(path,root)));} catch {res.writeHead(500);res.end();}
}).listen(8788,'127.0.0.1',()=>console.log('Synthetic dashboard preview: http://127.0.0.1:8788/admin/'));
