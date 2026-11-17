"""Exact saved trailer identity, notes, reopening and seek liveness; no displayed-FPS claim."""
import json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_review_session import call,WORKER
out=Path(__file__).with_name('native-saved-final-verification-04.json');assert not out.exists()
h='fa0befe66d02dde6e75bb67453ad954fd313b2e12fe2d7b74536a6e06282837a';rid='timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237';guard={'resourceId':rid,'documentHash':h}
result={'pid':WORKER['pid'],'app':WORKER['app'],'events':[],'complete':False,'scope':'Native functional identity, save/reopen, notes and guarded seeking; foreground interaction and physical playback separate.'}
def record(stage,value):
 result['events'].append({'stage':stage,'value':value});out.write_text(json.dumps(result,indent=2)+'\n');print(stage,flush=True)
def ready():
 end=time.monotonic()+180
 while True:
  d=call('timeline.inspect_document',{'includeDocumentHash':False})
  # Save inspection may deliberately withhold the stable hash while its
  # snapshot is in flight. Check any published identity, then wait for readiness.
  if d.get('resourceId'):assert d['resourceId']==rid
  if d.get('documentHash'):assert d['documentHash']==h
  if d['ready'] and not d['saveInProgress']:
   d=call('timeline.inspect_document')
   assert d['resourceId']==rid and d['documentHash']==h
   return d
  assert time.monotonic()<end,d
  time.sleep(3)
try:
 initial=ready();record('settledInitial',initial)
 notes=call('timeline.inspect_review_notes',{'maxItems':20});assert notes['returnedCount']==3;record('notesBefore',notes)
 if initial['dirty']:record('saveAccepted',call('timeline.save_active_draft',dict(guard,confirmed=True)))
 clean=ready();assert clean['saved'] and not clean['dirty'] and not clean['saveFailed'];record('cleanSaved',clean)
 record('closed',call('timeline.close_saved_draft',dict(guard,confirmed=True)))
 started=time.monotonic();opened=call('timeline.open_saved_draft',{'timelineId':rid[9:]})
 # Retain the bounded identity receipt, not thousands of full clip snapshots.
 record('opened',{'ok':opened['ok'],'operation':opened['operation'],'resourceId':opened.get('resourceId'),'rpcSeconds':time.monotonic()-started})
 d=ready();assert d['saved'] and not d['dirty'] and not d['saveFailed'] and d['trackCount']==196;record('reopenedClean',d)
 read=call('timeline.inspect_review_notes',{'maxItems':20});assert read['items']==notes['items'];record('notesIdentical',{'count':read['returnedCount']})
 for seconds in [5.0,45.733,62.5,148.5,218.5,289.0,218.5]:
  started=time.monotonic();r=call('timeline.preview_at_time',dict(guard,seconds=seconds));assert r['documentHashUnchanged'];record('seek',{'requestedSeconds':seconds,'receipt':r,'rpcSeconds':time.monotonic()-started})
 final=ready();assert not final['dirty'];record('finalClean',final)
 result['complete']=True;out.write_text(json.dumps(result,indent=2)+'\n');print('verified',flush=True)
except Exception as error:
 result['error']=str(error);out.write_text(json.dumps(result,indent=2)+'\n');raise
