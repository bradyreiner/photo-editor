"""Authenticated requests to the live app's MCP endpoint; credentials stay in memory."""
from pathlib import Path
import json,urllib.request,sys,os
# An isolated demo app has its own automation session. Select that exact app
# explicitly instead of accidentally authoring into the user's regular catalog.
SESSION=Path(os.environ.get('LOOKSY_NATIVE_MCP_SESSION',
 str(Path.home()/'Library/Application Support/PhotoEditor/automation/session.json')))
MAX_HTTP_BODY_BYTES=1024*1024
def encode_request(method,params=None):
 data=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params or {}}).encode()
 if len(data)>MAX_HTTP_BODY_BYTES:
  raise ValueError(f'Native MCP request is {len(data)} bytes; the app accepts at most {MAX_HTTP_BODY_BYTES}. Split the authored operation before execution.')
 return data
def validate_tool_request_size(name,args=None):
 return len(encode_request('tools/call',{'name':name,'arguments':args or {}}))
def request(method,params=None):
 data=encode_request(method,params)
 s=json.loads(SESSION.read_text());url=s.get('mcpEndpoint');assert url,'Native MCP endpoint is disabled'
 req=urllib.request.Request(url,data=data,headers={'Authorization':'Bearer '+s['token'],'Content-Type':'application/json','Accept':'application/json'})
 with urllib.request.urlopen(req,timeout=120) as response:return json.loads(response.read())
def call(name,args=None):return request('tools/call',{'name':name,'arguments':args or {}})
if __name__=='__main__':
 method=sys.argv[1];params=json.loads(sys.argv[2]) if len(sys.argv)>2 else {}
 print(json.dumps(request(method,params),indent=2))
