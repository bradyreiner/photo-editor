#!/usr/bin/env python3
"""Author unexecuted native-format canary inputs only. Never opens app/session/catalog."""
from pathlib import Path
import json,copy,hashlib,uuid
P=Path(__file__).resolve().parent
REPO=P.parents[4]
# A real native saved adjustment-state supplies the required neutral/compatibility structures.
# Only the canary's four explicit layers and empty mask/history lists are used for authoring.
EXAMPLE=REPO/'.photo-editor/nepal-near-far/production-native-document.json'
doc=json.loads(EXAMPLE.read_text())['document'];state=copy.deepcopy(doc['slides'][0]['primary']['adjustmentPatch']['adjustmentState'])
PHOTO=REPO/'marketing/animated-trailer/public/media/IMG_5071.jpg'
W,H=1080,1350
common={'enabled':True,'opacity':1,'blendMode':'normal','scale':1,'rotation':0,'canvasWidth':W,'canvasHeight':H,'solo':False,'locked':False}
def layer(i,kind,name,**extra):
 value=dict(common);value.update(extra);value.update(id=f'candidate-canary-{i}',kind=kind,name=name);return value
path={'schema':'photo-vector-path','version':1,'coordinateSpace':'normalized','subpaths':[{'id':'canary-rectangle','closed':True,'anchors':[{'id':f'corner-{j}','kind':'corner','handlesLinked':False,'point':[x/W,y/H],'in':[0,0],'out':[0,0]} for j,(x,y) in enumerate([(60,420),(1020,420),(1020,432),(60,432)])]}]}
layers=[layer(1,'text','Canary - editable headline',text='Native layers.\nReal control.',fontFamily='Space Grotesk',fontSize=100,fontWeight=600,textColor='#202522',textAlignment='left',pixelWidth=960,pixelHeight=240,x=60,y=145),layer(2,'pixel','Canary - editable vector blue rule',vectorPath=path,vectorFillEnabled=True,vectorFillColor='#3282FF',vectorStrokeEnabled=False,pixelWidth=W,pixelHeight=H,x=0,y=0),layer(3,'source','Canary - independent raw photograph',sourcePath=str(PHOTO),pixelWidth=1800,pixelHeight=1200,x=60,y=450,scale=960/1800),layer(4,'fill','Canary - editable paper',fillColor='#F1EFE7',fillAlpha=1,pixelWidth=W,pixelHeight=H,x=0,y=0)]
# Avoid double scale argument in source helper: explicit overwrite is intentionally data-only.
state['layers']=copy.deepcopy(layers);state['photoLayers']=copy.deepcopy(layers)
for k in ['maskStack','maskSets','retouchStack','presetApplications','overlayItems']:state[k]=[]
state['history']={'activeBranchId':'main','snapshots':[]}
state['maskSetSchemaVersion']=2
recipe=dict(state,recipeVersion=12)
clipboard={'schema':'looksy-photo-layers-v1','width':W,'height':H,'selectedIds':[l['id']for l in layers],'recipe':recipe}
(P/'canary-photo-layers-UNEXECUTED.json').write_text(json.dumps(clipboard,indent=2,ensure_ascii=False)+'\n')
bundle=P/'canary-preset-bundle';bundle.mkdir(exist_ok=True)
preset={'name':'LOOKSi Native Canary 2026-11-16','pack':'LOOKSi Native Authoring Canary','category':'Creative','adjustmentGroups':['layers'],'adjustmentPatch':{'format':'photo-editor-adjustment-patch','version':1,'scope':'source-agnostic','groups':['layers'],'adjustmentState':state}}
(bundle/'photo-editor-presets-sync.json').write_text(json.dumps({'format':'photo-editor-preset-sync','version':3,'presets':[preset]},indent=2,ensure_ascii=False)+'\n')
manifest={'status':'UNEXECUTED INPUTS - NOT A NATIVE PROJECT OR RECEIPT','canvas':[W,H],'expected_nonbase_layers':4,'layer_kinds':['text','pixel with native vectorPath','source photograph','fill'],'source':{'path':str(PHOTO),'sha256':hashlib.sha256(PHOTO.read_bytes()).hexdigest(),'kind':'camera JPEG preview photograph; primitive source only'},'native_structure_reference':{'path':str(EXAMPLE),'sha256':hashlib.sha256(EXAMPLE.read_bytes()).hexdigest()},'routes':{'preferred':'Live UI New Boards then Presets > Manage > Import Preset Bundle; choose candidate folder and apply canary','alternative':'Live UI native Paste Layers using MIME application/x-looksy-photo-layers-v1; same canvas size and Paste in Place'},'proof_required':['App imports/applies without validation error','Headline appears as editable native text','Photograph is an independent source layer, not a flattened cover','Blue rule has editable native vectorPath, not a raster screenshot','Change headline in app; Undo restores exact copy','Save Boards through app; Saved state and stable document ID recorded','Open actual exported image and inspect text/photo/rule','Open another document then reopen canary; native layers and source references retained'],'claims':{'app_imported':False,'app_edited':False,'app_saved':False,'app_reopened':False,'app_exported':False}}
(P/'canary-input-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
# Board-by-board authoring plans, not synthesized catalog records.
names=[('01','I wanted to make this.',6),('02','NEPAL. In three forms.',6),('03','Keep the gold. Keep the quiet blues.',7),('04','The last tweak. Again.',6),('05','Make room for your eye.',6)]
plans=[]
for i,title,count in names:
 plans.append({'campaign_id':i,'document_name':f'LOOKSi Campaign {i} - {title}','project_name':'LOOKSi native launch campaigns','native_workspace':'Edit Boards','new_canvas_settings':{'name':f'LOOKSi Campaign {i} - {title}','projectName':'LOOKSi native launch campaigns','width':W,'height':H,'resolution':144,'background':'color','color':'#F1EFE7'if i in['01','03']else'#101418'},'planned_board_count':count,'native_authoring_route':'UI creates/names the document; a source-agnostic layers preset or native layer clipboard is applied to each actual blank board. Native UI adds next blank board and saves. Current command admission must be live-discovered before optional MCP mutations.','allowed_sources':['Raw/source photographs with recorded paths/hashes','Original abstract art as a single declared ingredient','Native text, fill, vector and source layer definitions'],'forbidden_substitute':'Flattened V2 cover/carousel/film frame imported as the board artwork','proof_per_document':['Stable app-created document ID and name','Native layer tree and kinds per board','At least one live text edit and Undo','App Save Boards completed','App native export inspected','Document reopened and native layer definitions/source references retained'],'state':'PLANNED - not created or counted complete'})
(P/'five-native-document-plans.json').write_text(json.dumps({'status':'authoring plans only','documents':plans},indent=2,ensure_ascii=False)+'\n')
print('Wrote unexecuted native clipboard and preset inputs plus five document plans. No app/catalog/clipboard changes.')
