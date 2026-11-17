from pathlib import Path
import json
from native_mcp import call
out=Path(__file__).parent/'evidence'
def unpack(v):
 if 'error' in v:raise RuntimeError(v['error'])
 r=v.get('result',v)
 if 'structuredContent' in r:return r['structuredContent']
 if r.get('isError'):return r
 for c in r.get('content',[]):
  if c.get('type')=='text':
   try:return json.loads(c['text'])
   except json.JSONDecodeError:pass
 return r
if __name__=='__main__':
 commands=[];offset=0
 while True:
  raw=call('app.list_commands',{'domain':'timeline','offset':offset,'limit':64});j=unpack(raw)
  if 'commands' not in j:
   print(json.dumps(j)[:3000]);break
  commands+=j['commands'];n=j.get('nextOffset',-1)
  if n is None or n<0 or n<=offset:break
  offset=n
 (out/'timeline-live-catalogue.json').write_text(json.dumps(commands,indent=2)+'\n')
 print('Total',len(commands),'available',sum(c.get('available',False) for c in commands))
 for c in commands:
  if c.get('available') and any(x in c['name'].lower() for x in ['new','create','import','media','save','open','title','shape','effect','sequence','track','inspect','graphic','export','property','source','animation','layer']):print(c['commandId'],c.get('readOnly'))
