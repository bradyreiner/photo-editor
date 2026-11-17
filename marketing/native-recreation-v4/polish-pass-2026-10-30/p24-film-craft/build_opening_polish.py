import json,hashlib
from pathlib import Path
P=Path(__file__).resolve().parent
baseline=P/'before-native.json';d=json.loads(baseline.read_text());clips={c['clipId']:c for t in d['tracks'] for c in t['clips']}
admission=json.loads((P/'admission-result.json').read_text());intent=json.loads((P/'admission-intent.json').read_text());steps=[]
def channel(name,poses):
 return {'parameter':name,'keyframes':[{'localSeconds':round(t*30)/30,'value':v,'interpolation':ease} for t,v,ease in poses]}
def edit(id,label,*channels):
 assert id in clips and clips[id]['startSeconds']==0 and clips[id]['durationSeconds']==12
 steps.append({'id':'p24/opening/'+label,'commandId':'timeline.action.setTimelineClipAnimation','values':{'clipId':id,'channels':list(channels)}})
def opacity(start,settled,exit_start=10.5,exit_end=11.4,peak=1):
 return channel('opacity',[(0,0,'hold'),(start,0,'ease-out'),(settled,peak,'hold'),(exit_start,peak,'ease-in-out'),(exit_end,0,'hold'),(12,0,'hold')])
edit('6dcf29a7-0bbd-4975-a0cb-4ebe7b09e666','photo-reveal-and-camera',
 opacity(.55,1.45,10.7,11.8,.96),
 channel('frameScale',[(0,1.115,'ease-out'),(1.6,1.075,'linear'),(10.7,1.025,'ease-in-out'),(12,1.0,'hold')]),
 channel('frameOffsetX',[(0,.50,'ease-out'),(1.6,.46,'linear'),(10.7,.40,'ease-in-out'),(12,.38,'hold')]))
edit('b8ebcb54-b0d9-45c3-98f2-129740324084','light-motivates-reveal',
 channel('generator:gather',[(0,1,'hold'),(.2,1,'ease-in-out'),(1.5,0,'hold'),(10.5,0,'ease-in-out'),(12,.72,'hold')]),
 channel('opacity',[(0,.70,'hold'),(.5,.70,'ease-in-out'),(1.8,.24,'hold'),(10.5,.24,'ease-in-out'),(11.7,.62,'hold'),(12,.62,'hold')]))
edit('b227091d-1e51-457f-b205-6705442765c4','copy-contrast',
 channel('opacity',[(0,0,'ease-in-out'),(.9,.28,'hold'),(10.6,.28,'ease-in-out'),(11.7,0,'hold'),(12,0,'hold')]))
edit('6a128774-806c-4794-b6f8-7386a4b8ef1a','category',opacity(.1,.55,10.7,11.2),
 channel('frameOffsetY',[(0,.055,'ease-out'),(.55,0,'hold'),(12,0,'hold')]))
edit('cfe204a9-1543-420a-bf8e-514d51b2ccc2','headline-settle',opacity(.5,1.0,10.4,11.05),
 channel('frameOffsetY',[(0,.12,'hold'),(.5,.12,'ease-out'),(1.1,-.008,'ease-in-out'),(1.4,0,'hold'),(12,0,'hold')]))
edit('8874beed-5b9d-4abf-a2a0-4cf4cb60474f','support-follow-through',opacity(1.25,1.85,10.5,11.2),
 channel('frameOffsetY',[(0,.07,'hold'),(1.25,.07,'ease-out'),(1.85,0,'hold'),(12,0,'hold')]))
for n,(bg,label) in enumerate([
 ('b45af980-5569-4965-aaf6-4bf1340be95e','f812e495-10b2-428c-aaa5-49b4246289b7'),
 ('be95b513-982d-4a42-88de-c46f6e466f5a','8a5e1faa-dbd3-4568-a1a0-edb1a80b9c6f'),
 ('2a07b20e-300f-4819-b26b-e8bbcf18033c','ced37e04-92ec-4c3e-8c76-d748aa3ee3a0'),
 ('25700482-5244-4946-8559-e0aa17c953db','157425fa-c1d4-4e32-9418-e7474ec57033')]):
 start=2.25+n*.16;settled=start+.4
 for role,id in [('pill',bg),('label',label)]:
  edit(id,f'{n+1}-{role}',opacity(start,settled,10.55+n*.06,11.15+n*.06),
   channel('frameOffsetY',[(0,.055,'hold'),(start,.055,'ease-out'),(settled,0,'hold'),(12,0,'hold')]))
plan={'schema':'looksy.native-authoring-plan/v1','name':'P24 opening choreography: earlier clarity and connected light/photo/type motion','resourceId':intent['document']['resourceId'],'taskId':admission['taskId'],'startingDocumentHash':intent['document']['documentHash'],'composition':{'width':1920,'height':1080,'fps':30},'durationSeconds':298,
 'planningSnapshot':{'path':str(baseline),'sha256':hashlib.file_digest(baseline.open('rb'),'sha256').hexdigest(),'documentHash':intent['document']['documentHash']},
 'steps':steps,'preserve':['All source files and trims','Existing brand typography and title copy','All chapters after 12 seconds','New Gods and all authored audio','User saved monochrome rotation'],
 'review':['Photo is visible before 1.5 seconds','Headline settles before support copy; no overlap with logo','Each pill and label move as one object','Camera drift stays continuous during the hold','Existing transition veil owns the RAW handoff']}
assert len(steps)==14
out=P/'opening-polish-plan.json';assert not out.exists();out.write_text(json.dumps(plan,indent=2)+'\n')
print('Prepared',len(steps),'native operations for',len({s['values']['clipId'] for s in steps}),'existing clips')
