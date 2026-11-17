import ctypes,hashlib,json,os,sys
from pathlib import Path
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent));from native_review_session import call,save,WORKER
assert not (P/'p25-admission-result.json').exists(), 'Inspect the admitted task; never replay.'
assert not (P/'p25-admission-revised-intent.json').exists(), 'Inspect the corrected admission; never replay.'
doc=call('timeline.inspect_document');assert doc['ready'] and doc['documentHash']=='eba0b2291b5c4f9eda0f604b6f096f4f572feafee38fb3adc3e96552e7f907b9'
assert not call('timeline.inspect_task')['active']
source=P.parent/'native-projects/looksy-polish-pass24-opening-choreography.json';before=P/'p25-before-native.json'
lib=ctypes.CDLL('/usr/lib/libSystem.B.dylib',use_errno=True);lib.clonefile.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int];lib.clonefile.restype=ctypes.c_int
if not before.exists():
 assert lib.clonefile(os.fsencode(source),os.fsencode(before),0)==0
sha=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest();assert sha(source)==sha(before)
values={'targetResourceId':doc['resourceId'],'objective':'Polish the existing carousel export labels and video-timeline scrub demonstration using freshly verified native frames. Keep all footage, preview clock, playhead motion, audio, chapter order and the saved P24 opening choreography.',
'actions':[{'kind':'text','summary':'Center four export-chip labels. Suppress unreadable micro-captions and ease readable captions out and back in around the rapid video seek.'},{'kind':'framing','summary':'Change only named title alpha envelopes; preserve all video, camera, timeline and panorama geometry.'},{'kind':'delivery','summary':'Save and audit a native editable checkpoint and render the corrected moments.'}],
'assetPaths':[],'outputDirectory':str((P.parent/'native-projects').resolve()),'confirmed':True}
save(P/'p25-admission-revised-intent.json',{'document':doc,'before':str(before),'sha256':sha(before),'values':values,'priorRejection':'summary189characters exceeded native180character limit; fresh native task inspection confirms inactive before corrected admission'});task=call('timeline.admit_task',values);save(P/'p25-admission-result.json',task)
d=json.loads(before.read_text());cs={c['clipId']:c for t in d['tracks'] for c in t['clips']};steps=[]
def step(id,cmd,vals):steps.append({'id':'p25/'+id,'commandId':'timeline.action.'+cmd,'values':vals})
for n,id in enumerate(['2f97ef38-f8fc-4b6c-bc3d-adf2dc64ec49','302207d2-ef0e-4224-953f-fbdfcac4591f','70c7dfd3-d290-405b-b3b6-590dff7557dd','e944e431-1b55-448e-8606-267f7f7ed916']):
 assert cs[id]['outputVariantOverrides']['landscape']['titlePositionY']==.9111111111111111
 step('carousel-chip-'+str(n+1),'setTimelineClipTitle',{'clipId':id,'values':{'positionY':.9236111111111112}})
def alpha(id,label,poses):
 step(label,'setTimelineClipAnimation',{'clipId':id,'channels':[{'parameter':'opacity','keyframes':[{'localSeconds':round(t*30)/30,'value':v,'interpolation':ease} for t,v,ease in poses]}]})
for n,id in enumerate(['ab5f31b9-d532-416b-8009-a5d99d5468e2','42011e3d-6600-48ca-b3e9-ae38c6a5b4dd','67cffd4a-83ea-4dfa-b975-e41c51836284','d356330a-6614-4a19-8658-f14238eedf1a']):
 assert cs[id]['durationSeconds']<=.201
 alpha(id,'suppress-unreadable-scrub-caption-'+str(n+1),[(0,0,'hold'),(cs[id]['durationSeconds'],0,'hold')])
for n,id in enumerate(['0147a602-a816-4c24-9135-8a8e522f81b6','782a35c5-8a3e-488e-b29e-1d5020d1c454']):
 assert cs[id]['startSeconds']==196
 alpha(id,'night-caption-clear-'+str(n+1),[(0,1,'hold'),(1.4,1,'ease-in-out'),(cs[id]['durationSeconds'],0,'hold')])
for n,id in enumerate(['36537cba-3758-437f-b681-b9dab037eaad','eea61106-fa50-460b-8290-32d01ff66fd0']):
 assert cs[id]['startSeconds']==198
 alpha(id,'arrival-caption-settle-'+str(n+1),[(0,0,'ease-out'),(7/30,1,'hold'),(cs[id]['durationSeconds'],1,'hold')])
plan={'schema':'looksy.native-authoring-plan/v1','name':'P25 native carousel label alignment and readable video scrub captions','resourceId':doc['resourceId'],'taskId':task['taskId'],'startingDocumentHash':doc['documentHash'],'composition':{'width':1920,'height':1080,'fps':30},'durationSeconds':298,'planningSnapshot':{'path':str(before),'sha256':sha(before),'documentHash':doc['documentHash']},'steps':steps,'nativeEvidence':['native-p24-carousel-survey-1s/frame-5040.png','native-p24-video-scrub-survey/frame-5931.png'],'preserve':['Every media source, trim and video preview clock','P24 opening','All audio and New Gods','All panorama and timeline geometry','User saved monochrome rotation']}
assert len(steps)==12
save(P/'p25-polish-plan.json',plan)
os.environ['LOOKSY_NATIVE_MCP_SESSION']=str(Path(WORKER['data'])/'automation/session.json');sys.path.insert(0,str(P.parent.parent));from author_native import execute
execute(P/'p25-polish-plan.json',P/'p25-polish-execution.json',compact_inspection=True)
