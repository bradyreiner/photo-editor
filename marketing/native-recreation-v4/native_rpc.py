"""Use Looksy's authenticated local automation transport; never print its token."""
from pathlib import Path
import socket,json,sys
SESSION=Path.home()/'Library/Application Support/PhotoEditor/automation/session.json'
def call(command,args=None):
 session=json.loads(SESSION.read_text());request={'id':1,'token':session['token'],'command':command,'args':args or {}}
 with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
  s.settimeout(60);s.connect(session['fullServerName']);s.sendall(json.dumps(request).encode()+b'\n');data=b''
  while True:
   b=s.recv(1024*1024)
   if not b:break
   data+=b
   if data.endswith(b'\n'):break
 return json.loads(data)
if __name__=='__main__':
 print(json.dumps(call(sys.argv[1],json.loads(sys.argv[2]) if len(sys.argv)>2 else {}),indent=2))
