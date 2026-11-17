import json,sys
from pathlib import Path
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent))
from native_review_session import call,save
plan=json.loads((P/'opening-polish-plan.json').read_text());doc=call('timeline.inspect_document');plan['startingDocumentHash']=doc['documentHash'];plan['name']='P24 opening: align all exits with the existing 8–9.6s desktop reveal'
for step in plan['steps']:
 label=step['id'].rsplit('/',1)[-1];step['id']+='-handoff'
 for ch in step['values']['channels']:
  if ch['parameter']=='opacity':
   frames=ch['keyframes']
   if label=='photo-reveal-and-camera':start,end=7.9,8.8
   elif label=='headline-settle':start,end=7.7,8.35
   elif label=='support-follow-through':start,end=7.8,8.45
   elif label=='category':start,end=7.9,8.5
   elif label=='copy-contrast':start,end=7.8,8.6
   elif label=='light-motivates-reveal':start,end=7.8,8.7
   else:
    n=int(label.split('-')[0])-1;start,end=7.8+n*.04,8.4+n*.04
   late=[k for k in frames if 9.6<=k['localSeconds']<12]
   assert len(late)==2,(label,late)
   late[0]['localSeconds']=round(start*30)/30;late[1]['localSeconds']=round(end*30)/30
  elif ch['parameter']=='generator:gather':
   ch['keyframes'][-2]['localSeconds']=7.8
   ch['keyframes'].insert(-1,{'localSeconds':8.7,'value':.72,'interpolation':'hold'})
  elif label=='photo-reveal-and-camera' and ch['parameter'] in ['frameOffsetX','frameScale']:
   ch['keyframes'][-2]['localSeconds']=8.8
plan['review'].append('All opening typography clears while the preserved desktop reveal group enters at 8–9.6 seconds; no long overlap between the two title systems.')
save(P/'opening-handoff-plan.json',plan)
import os
from native_review_session import WORKER
os.environ['LOOKSY_NATIVE_MCP_SESSION']=str(Path(WORKER['data'])/'automation/session.json')
sys.path.insert(0,str(P.parent.parent));from author_native import execute
execute(P/'opening-handoff-plan.json',P/'opening-handoff-execution.json',compact_inspection=True)
