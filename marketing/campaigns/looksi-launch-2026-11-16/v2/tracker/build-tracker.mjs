import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync, spawnSync } from 'node:child_process';
import sharp from 'sharp';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const root=path.resolve(import.meta.dirname,'..');
const scannedAt=new Date();
const qa=path.join(root,'tracker','qa');await fs.mkdir(qa,{recursive:true});
const wb=Workbook.create();
const campaigns=wb.worksheets.add('Campaigns');
const assets=wb.worksheets.add('Assets');
const tasks=wb.worksheets.add('Production tasks');
const launch=wb.worksheets.add('Launch log');
const c={ink:'#17191C',muted:'#5B636D',line:'#E0E4E8',amber:'#FFF0CB',red:'#FCE5E4'};
const specs=[
 {id:'C01',folder:'campaign01',name:'I wanted to make this.',owner:'Builder',proof:'Finished text-led builder letter with real work and dated native archive inserts. Optional founder performance is a separate future asset.'},
 {id:'C02',folder:'campaign02',name:'NEPAL. In three forms.',owner:'Editor',proof:'Mixed-session output casebook: native photo and six boards on Nov 16; native 12-second Timeline export on Nov 2. A fresh continuous walkthrough is optional.'},
 {id:'C03',folder:'campaign03',name:'Keep the gold. Keep the quiet blues.',owner:'Editor',proof:'Actual native before and after. The combined treatment is 5900 K, contrast +12, exposure +0.15 EV. No isolated-setting, Undo or screen-recording claim.'},
 {id:'C04',folder:'campaign04',name:'The last tweak. Again.',owner:'Motion director',proof:'Finished original graphic film: 24 seconds, three hooks, sound-on and silent masters. Campaign artwork, not app output. No filming needed.'},
 {id:'C05',folder:'campaign05',name:'Make room for your eye.',owner:'Creative director',proof:'36-second photographic manifesto with real photos, a dated native product moment and original graphic-light transitions.'},
];
function base(s,last,row,title,widths){
 s.showGridLines=false;s.tabColor=c.ink;
 const r=s.getRange(`A1:${last}${row}`);r.format.font={name:'Arial',size:10,color:c.ink};r.format.verticalAlignment='center';r.format.rowHeight=24;
 widths.forEach((v,i)=>s.getRangeByIndexes(0,i,row,1).format.columnWidth=v);
 s.getRange('A2').values=[[title]];s.getRange('A2').format.font={name:'Arial',size:16,bold:true};
 s.getRange(`A3:${last}3`).format.borders={bottom:{style:'thin',color:c.line}};
 s.freezePanes.freezeRows(8);s.freezePanes.freezeColumns(2);
}
function header(s,last,labels){const r=s.getRange(`A8:${last}8`);r.values=[labels];r.format={fill:c.ink,font:{name:'Arial',size:10,bold:true,color:'#FFFFFF'},wrapText:true,horizontalAlignment:'center',verticalAlignment:'center',rowHeight:38};}
function input(r,values){r.format.fill=c.amber;if(values)r.dataValidation={rule:{type:'list',values}};}
function warn(r,formula){r.conditionalFormats.addCustom(formula,{fill:c.red,font:{color:'#8D2520',bold:true}});}
function table(s,range,name){s.tables.add(range,true,name).style='TableStyleLight1';}
async function present(p){try{return (await fs.stat(p)).isFile()&&(await fs.stat(p)).size>0;}catch{return false;}}
async function walk(dir){let out=[];try{for(const d of await fs.readdir(dir,{withFileTypes:true})){if(['qa','fonts','__pycache__','node_modules','frames','motion-frames','curve-frames','previews'].includes(d.name))continue;const p=path.join(dir,d.name);if(d.isDirectory())out.push(...await walk(p));else if(d.isFile()&&!d.name.endsWith('.pyc'))out.push(p);}}catch{}return out;}
const reserved=new Set();const records=[];const primaryValidation=[];
const expectedDurations={C01:32,C02:42,C03:33,C04:24,C05:36};
async function record(spec,rel,role,kind){
 const absolute=path.join(root,rel);let bytes=null,duration=null,width=null,height=null;
 const exists=await present(absolute);
 const initialStat=exists?await fs.stat(absolute):null;
 if(exists){bytes=initialStat.size;
  try{if(/\.(mp4|mov)$/i.test(rel)){const p=JSON.parse(execFileSync('ffprobe',['-v','error','-show_entries','format=duration:stream=codec_type,width,height','-of','json',absolute],{encoding:'utf8',stdio:['ignore','pipe','ignore']}));const v=p.streams.find(x=>x.codec_type==='video');duration=Number(p.format.duration);width=v?.width??null;height=v?.height??null;}
  else if(/\.(png|jpg|jpeg|svg)$/i.test(rel)){const m=await sharp(absolute).metadata();width=m.width??null;height=m.height??null;}}catch{}
 }
 let basicMetadataValid=kind==='Video'?Number.isFinite(duration)&&duration>0&&width>0&&height>0:['Image','Storyboard','SVG','Carousel'].includes(kind)?width>0&&height>0:true;
 if(role==='Primary'&&exists){
  const check={campaign:spec.id,path:rel,kind,fullDecode:false,stableFile:false};
  try{
   if(kind==='Video'){
    const counted=JSON.parse(execFileSync('ffprobe',['-v','error','-count_frames','-show_entries','stream=codec_type,codec_name,r_frame_rate,nb_read_frames,color_range,color_space,color_transfer,color_primaries','-of','json',absolute],{encoding:'utf8',stdio:['ignore','pipe','pipe']}));
    const v=counted.streams.find(v=>v.codec_type==='video');const [n,d]=v.r_frame_rate.split('/').map(Number);const fps=n/d;
    const expectedFrames=Math.round(expectedDurations[spec.id]*fps);
    if(v.codec_name!=='h264'||width!==1080||height!==1920||Math.abs(duration-expectedDurations[spec.id])>.05||Number(v.nb_read_frames)!==expectedFrames)throw new Error('Unexpected primary video metadata or frame count');
    if(v.color_range!=='tv'||v.color_space!=='bt709'||v.color_transfer!=='bt709'||v.color_primaries!=='bt709')throw new Error('Primary video needs explicit BT.709/limited metadata');
    const decoded=spawnSync('ffmpeg',['-hide_banner','-v','error','-i',absolute,'-f','null','-'],{encoding:'utf8',stdio:['ignore','pipe','pipe']});
    if(decoded.status!==0||decoded.stderr?.trim())throw new Error('Primary video full-decode check failed');
    Object.assign(check,{seconds:duration,fps,decodedFrames:Number(v.nb_read_frames),colorRange:v.color_range,colorSpace:v.color_space,colorTransfer:v.color_transfer,colorPrimaries:v.color_primaries});
   }else{
    await sharp(absolute).raw().toBuffer();
    if(kind==='Image'&&(width!==1080||height!==1350))throw new Error('Unexpected primary cover size');
    Object.assign(check,{width,height});
   }
   const afterStat=await fs.stat(absolute);check.stableFile=afterStat.size===initialStat.size&&afterStat.mtimeMs===initialStat.mtimeMs;
   check.fullDecode=check.stableFile;
  }catch(e){check.error=String(e.message).slice(0,160);}
  basicMetadataValid=basicMetadataValid&&check.fullDecode;
  primaryValidation.push(check);
 }
 const state=exists?(basicMetadataValid?'Present':'Inspect file'):'Missing';
 const id=`${spec.id}-${crypto.createHash('sha1').update(rel).digest('hex').slice(0,6)}`;
 records.push([id,spec.id,role,kind,state,rel,spec.proof,state==='Present'?(role==='Primary'?'Review delivered primary':'Retain source or select variant'):state==='Inspect file'?'Finish encode or inspect file':'Produce or link primary export',duration,width,height,bytes]);
}
for(const spec of specs){
 for(const [p,kind] of [['images/cover.png','Image'],['storyboard/board.png','Storyboard'],['videos/hero.mp4','Video']]){const rel=`${spec.folder}/${p}`;reserved.add(rel);await record(spec,rel,'Primary',kind);}
}
for(const spec of specs){
 for(const p of (await walk(path.join(root,spec.folder))).sort()){
  const rel=path.relative(root,p);if(reserved.has(rel))continue;
  const ext=path.extname(p).toLowerCase();
  const role=/\.(svg)$/.test(ext)?'Editable':/\/(videos?|carousels?|slides)\//.test('/'+rel)&&/\.(mp4|png|jpg)$/.test(ext)?'Variant':'Source';
  const kind=ext==='.svg'?'SVG':/\.(mp4|mov)$/.test(ext)?'Video':/\.(png|jpg|jpeg)$/.test(ext)?(/\/(carousels?|slides)\//.test('/'+rel)?'Carousel':'Image'):ext==='.wav'?'Audio':/\.(py|m|js|mjs)$/.test(ext)?'Code':ext==='.json'?'Provenance / timing':'Support';
  await record(spec,rel,role,kind);
 }
}
const failed='media/exports/finish/nepal-mountain-working-copy-full-resolution.png';
for(const p of (await walk(path.join(root,'media'))).sort()){
 const rel=path.relative(root,p);if(rel===failed||rel===failed+'.integrity.json')continue;
 await record({id:'Shared',proof:'Shared original media or native export evidence. Excludes the first identical finish export. Retain original session dates.'},rel,'Source',/\.(mp4|mov)$/.test(rel)?'Video':/\.(png|jpg|jpeg)$/.test(rel)?'Image':'Evidence');
}
const endA=8+records.length;
base(assets,'L',endA+2,'LOOKSi V2 asset register',[19,13,14,19,17,68,75,43,12,12,12,16]);
assets.getRange('A4').values=[['Asset files']];assets.getRange('B4').formulas=[[`=COUNTA(A9:A${endA})`]];
assets.getRange('D4').values=[['Present files']];assets.getRange('E4').formulas=[[`=COUNTIF(E9:E${endA},"Present")`]];
assets.getRange('G4').values=[['Primary file records']];assets.getRange('H4').formulas=[[`=COUNTIF(C9:C${endA},"Primary")`]];
assets.getRange('I4').values=[['Scan UTC']];assets.getRange('J4:L4').merge();assets.getRange('J4').values=[[scannedAt]];assets.getRange('J4').setNumberFormat('yyyy-mm-dd hh:mm');
assets.getRange('A6').values=[['Primary files require full decode, expected cover/video size, expected video duration/frame count and a stable file. Sources/variants use basic metadata. Creative approval is separate.']];assets.getRange('A6').format.font.italic=true;
header(assets,'L',['Asset ID','Campaign','Role','Kind','File state','Relative path','Evidence / source meaning','Next action','Seconds','Width px','Height px','Bytes']);
assets.getRange(`A9:L${endA}`).values=records;
assets.getRange(`A9:L${endA}`).format.rowHeight=62;assets.getRange(`D9:H${endA}`).format.wrapText=true;
assets.getRange(`I9:I${endA}`).setNumberFormat('0.0');assets.getRange(`J9:L${endA}`).setNumberFormat('#,##0');assets.getRange(`I9:L${endA}`).format.horizontalAlignment='right';
warn(assets.getRange(`E9:E${endA}`),'=E9<>"Present"');table(assets,`A8:L${endA}`,'AssetFiles');

const before='media/exports/baseline/nepal-mountain-working-copy-full-resolution.png';
const after='media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png';
let photoProof=false;let photoComparison=null;
try{const b=await fs.readFile(path.join(root,before)),a=await fs.readFile(path.join(root,after));const receipt=JSON.parse(await fs.readFile(path.join(root,after+'.integrity.json'),'utf8'));
 const br=await sharp(b).removeAlpha().raw().toBuffer({resolveWithObject:true}),ar=await sharp(a).removeAlpha().raw().toBuffer({resolveWithObject:true});
 const sameDimensions=br.info.width===ar.info.width&&br.info.height===ar.info.height&&br.info.channels===ar.info.channels;
 const differentPixels=sameDimensions&&!br.data.equals(ar.data);
 photoComparison={sameDimensions,differentDecodedPixels:differentPixels,width:ar.info.width,height:ar.info.height};
 photoProof=receipt.valid===true&&crypto.createHash('sha256').update(a).digest('hex')===receipt.sha256&&differentPixels;
}catch{}
let graphicProof=false;try{const q=JSON.parse(await fs.readFile(path.join(root,'campaign04/qa/verification.json'),'utf8'));graphicProof=Object.values(q.videos).length===6&&Object.values(q.videos).every(v=>v.full_decode==='passed');}catch{}
// Owner labels are production roles. Dates are intentionally unassigned.
const taskRows=[
 ['G01','All','Primary','Review campaign names and final copy','Builder','Pending','Review final captions and naming','Intended claims and maker disclosure are approved',0.5,null,'campaigns.json; copybook.md; source/copy-and-testing.json'],
 ['G02','All','Primary','Select channel and check rules','Producer','Pending','Choose one appropriate placement','Current posting rules permit the selected founder post',0.5,null,'research/research-v2.md'],
 ['G03','All','Primary','Confirm response route','Builder','Pending','Choose comments, profile or a tested destination','Comment CTA works without a purchase link; any linked destination tested',0.25,null,'Current campaigns use feedback or following the build'],
 ['G04','All','Primary','Review phone, audio and accessibility','Editor','Pending','Play selected master and review cover at phone size','Essential type readable; audio and silent versions correct; alt text retained',0.75,null,'Campaign QA, caption and transcript files'],
 ['G05','All','Primary','Review source provenance and dates','Producer','Pending','Check used files and native session labels','Owned/source status and original capture/export dates are explicit',0.5,null,'Campaign source records; media/exports receipts'],
 ['01P','C01','Primary','Review completed builder letter','Builder','Pending','Review the text-led letter and native archive labels','Primary letter has no founder placeholder; old footage is date-labeled',0.5,null,'campaign01 primary master and sources'],
 ['01F','C01','Optional','Film 40-second founder performance','Builder','Pending','Record three natural takes of a true personal reason','Eye-level vertical framing; quiet voice; 2-second handles; no invented backstory',0.75,null,'Optional future version; does not block primary letter'],
 ['01E','C01','Optional','Cut founder performance','Editor','Pending','Select one take; add accurate captions and real inserts','Speech, dates and inserts agree; no placeholders; phone playback reviewed',1.5,null,'Depends on 01F; distinct future asset'],
 ['01N','C01','Optional','Record a fresh app insert','Editor','Pending','Capture one current-build action and opened output','Real cursor-to-action continuity; source and build recorded; preserve unsaved state',1,null,'Fresh upgrade; dated archive already supports the primary letter'],
 ['02P','C02','Primary','Review output casebook receipts','Editor','Pending','Check photo, six boards and dated Timeline receipt','Three finished forms are traceable; Timeline remains labeled Nov 2, 2026',0.75,null,'Nov 16 photo/boards; Nov 2 Timeline 12s, 1920x1080, 30fps, 360 frames'],
 ['02L','C02','Primary','Review mixed-session language','Builder','Pending','Approve title, captions and casebook labels','No all-session live walkthrough or automatic cross-workspace sync claim',0.5,null,'NEPAL. In three forms. A photo, motion study and six boards'],
 ['02N','C02','Optional','Capture a fresh continuous project walkthrough','Editor','Pending','Record photo, Timeline and board routes with exports','Same source project and build; uncut master retained; exports opened; no forced app quit',3,null,'Optional upgrade; existing Timeline window is alive but inaccessible'],
 ['02V','C02','Optional','Record professional explanation','Builder','Pending','Explain one visible choice per output','Plain language; only demonstrated controls and actual output stated',0.75,null,'Optional narrated walkthrough version'],
 ['03P','C03','Primary','Verify native before/after export pair','Editor',photoProof?'Complete':'Pending','Retain baseline and verified -2 finish receipts','Different pixels; finish SHA-256 matches receipt; failed identical finish excluded',0.25,null,`${before}; ${after}`],
 ['03L','C03','Primary','Review combined-settings lesson','Builder','Pending','Check recipe and explanation against the actual result','5900 K, contrast +12, exposure +0.15 EV described as one combined treatment',0.5,null,'No isolated-setting proof, screen recording or Undo demonstration asserted'],
 ['03N','C03','Optional','Record a beginner-paced control demonstration','Editor','Pending','Capture the actual controls and comparison slowly','Visible current-build actions and output; preserve unsaved state; show only real comparisons',1.5,null,'Optional screen walkthrough; primary is actual native result breakdown'],
 ['03V','C03','Optional','Record learner voiceover','Builder','Pending','Explain the intended warmth and retained cool blues','Quiet natural voice; no universal preset claim; text still carries the lesson',0.5,null,'Optional voice; no face required'],
 ['04P','C04','Primary','Verify all graphic-film masters','Motion director',graphicProof?'Complete':'Pending','Retain six-master decode and sound-level evidence','Three 24s hooks; sound-on and silent masters decode fully',0.25,null,'campaign04/qa/verification.json'],
 ['04L','C04','Primary','Review the perfectionism joke','Builder','Pending','Approve pacing, copy and final invitation','Poster is campaign artwork; no version-management, speed or app-output promise',0.25,null,'campaign04/source/copy-and-testing.json'],
 ['04R','C04','Optional','Choose a hook for a controlled iteration','Producer','Pending','Compare one opening variable within one channel','Same body and observation window; no duplicate promotional burst',0.25,null,'campaign04 videos/hero, hook-b and hook-c'],
 ['05P','C05','Primary','Review real-photo and native-moment provenance','Creative director','Pending','Confirm source ownership and dated product insert','Real photos and dated native moment labeled; graphic light is identified',0.5,null,'campaign05 source records'],
 ['05L','C05','Primary','Review manifesto pacing and type','Builder','Pending','Watch the 36-second film with sound on and off','Seeing, making and maker invitation stay clear at phone size',0.5,null,'campaign05 primary master and storyboard'],
 ['05N','C05','Optional','Film fresh observational inserts','Builder','Pending','Shoot light, a chosen frame and hands making a decision','Three stable 8–12s takes; owned subject; 2-second handles; no equipment endorsement',1,null,'Optional original footage upgrade; primary uses real photos and dated native moment'],
 ['R01','Sales','Sales only','Finalize commercial offer','Builder','Pending','Approve any pricing and checkout before sales copy','Terms approved; price and destination verified before an access or purchase claim',1,null,'release/product.json currently has blank prices/checkout and unapproved terms'],
 ['R02','Sales','Sales only','Qualify release availability','Builder','Pending','Verify installation and distribution before availability copy','Supported release and actual access journey checked',2,null,'Future sales release; not a prerequisite for these feedback campaigns'],
];
const endT=8+taskRows.length;
base(tasks,'K',endT+2,'LOOKSi V2 production tasks',[11,12,15,37,22,17,51,70,13,16,75]);
tasks.getRange('A4').values=[['Open primary']];tasks.getRange('B4').formulas=[[`=COUNTIFS(C9:C${endT},"Primary",F9:F${endT},"<>Complete")`]];
tasks.getRange('D4').values=[['Plan hours, primary']];tasks.getRange('E4').formulas=[[`=SUMIF(C9:C${endT},"Primary",I9:I${endT})`]];
tasks.getRange('G4').values=[['Plan hours, optional']];tasks.getRange('H4').formulas=[[`=SUMIF(C9:C${endT},"Optional",I9:I${endT})`]];
tasks.getRange('J4').values=[['Sales hours']];tasks.getRange('K4').formulas=[[`=SUMIF(C9:C${endT},"Sales only",I9:I${endT})`]];
tasks.getRange('A6').values=[['Hours are planning estimates. Owners are roles, not staffing assignments. Optional filming and Sales only tasks do not block primary feedback campaigns. Dates are unassigned.']];tasks.getRange('A6').format.font.italic=true;
header(tasks,'K',['Task ID','Scope','Priority','Work','Owner role','Status','Next action','Acceptance criteria','Plan hours','Target date','Evidence / dependency']);
tasks.getRange(`A9:K${endT}`).values=taskRows;tasks.getRange(`A9:K${endT}`).format.rowHeight=78;tasks.getRange(`C9:E${endT}`).format.wrapText=true;tasks.getRange(`G9:H${endT}`).format.wrapText=true;tasks.getRange(`K9:K${endT}`).format.wrapText=true;
input(tasks.getRange(`F9:F${endT}`),['Pending','In progress','Complete']);input(tasks.getRange(`J9:J${endT}`));tasks.getRange(`I9:I${endT}`).setNumberFormat('0.00');tasks.getRange(`J9:J${endT}`).setNumberFormat('mm/dd/yy');
warn(tasks.getRange(`F9:F${endT}`),'=AND($C9="Primary",F9<>"Complete")');table(tasks,`A8:K${endT}`,'ProductionTasksV2');

base(campaigns,'G',25,'LOOKSi V2 campaign production',[11,36,16,20,24,20,47]);
[60,225,78,130,150,110,250].forEach((v,i)=>campaigns.getRangeByIndexes(0,i,25,1).format.columnWidthPx=v);
campaigns.getRange('A4').values=[['Core files']];campaigns.getRange('B4').formulas=[[`=COUNTIFS(Assets!$C$9:$C$${endA},"Primary",Assets!$E$9:$E$${endA},"Present")`]];
campaigns.getRange('D4').values=[['Approved']];campaigns.getRange('E4').formulas=[['=COUNTIF(D9:D13,"Approved")']];
campaigns.getRange('F4').values=[['Ready']];campaigns.getRange('G4').formulas=[['=COUNTIF(E9:E13,"Ready to schedule")']];
campaigns.getRange('A6').values=[['Five primary campaigns. Amber review fields are editable. Optional founder shoots and fresh walkthroughs remain separate upgrades.']];campaigns.getRange('A6').format.font.italic=true;
header(campaigns,'G',['ID','Campaign','Core files','Creative review','Primary state','Owner role','Next step']);
campaigns.getRange('A9:G13').values=specs.map(s=>[s.id,s.id==='C03'?'Keep the gold.\nKeep the quiet blues.':s.name,null,'Pending',null,s.owner,null]);
campaigns.getRange('A9:G13').format.rowHeight=58;campaigns.getRange('B9:G13').format.wrapText=true;
input(campaigns.getRange('D9:D13'),['Pending','Changes requested','Approved']);
for(let r=9;r<=13;r++){
 const count=`COUNTIFS(Assets!$B$9:$B$${endA},A${r},Assets!$C$9:$C$${endA},"Primary",Assets!$E$9:$E$${endA},"Present")`;
 const open=`COUNTIFS('Production tasks'!$B$9:$B$${endT},"All",'Production tasks'!$C$9:$C$${endT},"Primary",'Production tasks'!$F$9:$F$${endT},"<>Complete")+COUNTIFS('Production tasks'!$B$9:$B$${endT},A${r},'Production tasks'!$C$9:$C$${endT},"Primary",'Production tasks'!$F$9:$F$${endT},"<>Complete")`;
 campaigns.getRange(`C${r}`).formulas=[[`=${count}&" / 3"`]];
 campaigns.getRange(`E${r}`).formulas=[[`=IF(${count}<3,"Assets open",IF(D${r}<>"Approved","Review open",IF(${open}>0,"Production open","Ready to schedule")))`]];
 campaigns.getRange(`G${r}`).formulas=[[`=IF(E${r}="Assets open","Finish missing primary exports",IF(E${r}="Review open","Review cover, storyboard and master",IF(E${r}="Production open","Complete required primary tasks","Assign channel and publication date")))`]];
}
warn(campaigns.getRange('E9:E13'),'=E9<>"Ready to schedule"');table(campaigns,'A8:G13','CampaignsV2');
campaigns.getRange('A16').values=[['Primary execution and evidence']];campaigns.getRange('A16').format.font.bold=true;
campaigns.getRange('A17:B21').values=specs.map(s=>[s.id,s.proof]);campaigns.getRange('B17:B21').format.wrapText=true;
// Readable evidence notes span one intentionally wide text column below the compact table.
for(let i=0;i<specs.length;i++){campaigns.getRange(`B${17+i}:G${17+i}`).merge();campaigns.getRange(`B${17+i}`).values=[[specs[i].proof]];campaigns.getRange(`A${17+i}:G${17+i}`).format.rowHeight=42;}
campaigns.getRange('A23:G23').merge();campaigns.getRange('A23').values=[['Filter Assets by role or kind. Update creative review and task status after review. Assign target dates once the publication schedule is chosen.']];campaigns.getRange('A23').format.wrapText=true;campaigns.getRange('A23:G23').format.rowHeight=34;

base(launch,'AA',28,'LOOKSi V2 launch measurements',[13,13,18,19,28,16,16,16,16,17,20,20,16,16,19,17,18,18,18,20,20,17,20,60,40,60,60]);
launch.getRange('A4').values=[['Published']];launch.getRange('B4').formulas=[['=COUNT(F9:F13)']];
launch.getRange('D4').values=[['Observation rows']];launch.getRange('E4').formulas=[['=COUNTA(A9:A13)']];
launch.getRange('A6').values=[['One row per post, variant and observation window. Record actuals only. Keep channel, format, denominator and elapsed hours comparable. Blank means unmeasured; zero is a measured zero.']];launch.getRange('A6').format.font.italic=true;
header(launch,'AA',['Post ID','Campaign','Platform','Format','Hook / source tag','Post date','Snapshot date','Window hours','Reach','Impressions','Qualified demo visits','Useful feedback','Saves','Shares','Profile visits','3s views','Completions','Avg watch sec','Video sec','Primary actions','Actions / 1,000','Rate basis','Completion rate','Metric source / definitions','Post URL','Learning / next action','Demo qualification rule']);
launch.getRange('A9:AA13').values=specs.map((s,i)=>[`L0${i+1}`,s.id,...Array(25).fill(null)]);
launch.getRange('A9:AA13').format.rowHeight=66;input(launch.getRange('C9:S13'));input(launch.getRange('X9:AA13'));launch.getRange('X9:AA13').format.wrapText=true;
input(launch.getRange('B9:B13'),specs.map(s=>s.id));input(launch.getRange('C9:C13'),['Instagram','Reddit','YouTube Shorts','LinkedIn','Other']);input(launch.getRange('D9:D13'),['Text post','Image post','Carousel','Reel / Short','Other']);
launch.getRange('F9:G13').setNumberFormat('mm/dd/yy');launch.getRange('H9:Q13').setNumberFormat('#,##0');launch.getRange('R9:S13').setNumberFormat('0.0');launch.getRange('T9:T13').setNumberFormat('#,##0');launch.getRange('U9:U13').setNumberFormat('0.0');launch.getRange('W9:W13').setNumberFormat('0.0%');launch.getRange('H9:W13').format.horizontalAlignment='right';
launch.dataValidations.add({range:'H9:S13',rule:{type:'decimal',operator:'greaterThanOrEqual',formula1:0}});
for(let r=9;r<=13;r++){
 const source=`IF(B${r}="C01",L${r},IF(B${r}="C02",L${r},IF(B${r}="C03",M${r},IF(B${r}="C04",N${r},IF(B${r}="C05",O${r},"")))))`;
 // ISNUMBER gates preserve blank actuals and measured numeric zero.
 const valid=`IF(B${r}="C01",ISNUMBER(L${r}),IF(B${r}="C02",ISNUMBER(L${r}),IF(B${r}="C03",ISNUMBER(M${r}),IF(B${r}="C04",ISNUMBER(N${r}),IF(B${r}="C05",ISNUMBER(O${r}),FALSE)))))`;
 launch.getRange(`T${r}`).formulas=[[`=IF(${valid},${source},"")`]];
 launch.getRange(`U${r}`).formulas=[[`=IF(ISNUMBER(T${r}),IF(ISNUMBER(I${r}),IF(I${r}=0,"n.a.",T${r}/I${r}*1000),IF(ISNUMBER(J${r}),IF(J${r}=0,"n.a.",T${r}/J${r}*1000),"")),"")`]];
 launch.getRange(`V${r}`).formulas=[[`=IF(ISNUMBER(I${r}),"Reach",IF(ISNUMBER(J${r}),"Impressions",""))`]];
 launch.getRange(`W${r}`).formulas=[[`=IF(AND(ISNUMBER(P${r}),ISNUMBER(Q${r})),IF(P${r}=0,"n.a.",Q${r}/P${r}),"")`]];
}
table(launch,'A8:AA13','LaunchObservationsV2');
launch.getRange('A16:G16').merge();launch.getRange('A16').values=[['Primary action definitions']];launch.getRange('A16').format.font.bold=true;
launch.getRange('A17:C21').values=[['C01','Useful workflow replies','Useful feedback: distinct people naming a real workflow or problem.'],['C02','Qualified demo/output questions','Useful feedback: distinct people asking a specific demonstration or output question.'],['C03','Saves','Platform-reported saves for this exact post and window.'],['C04','Shares','Platform-reported shares for this exact post and window.'],['C05','Profile visits','Post-attributed profile visits where the platform exposes them.']];
launch.getRange('B17:B21').format.wrapText=true;
for(let r=17;r<=21;r++){launch.getRange(`C${r}:G${r}`).merge();launch.getRange(`C${r}`).format.wrapText=true;launch.getRange(`A${r}:G${r}`).format.rowHeight=48;}
launch.getRange('I16:AA16').merge();launch.getRange('I16').values=[['Measurement rules']];launch.getRange('I16').format.font.bold=true;
launch.getRange('I17').values=[['Actions per 1,000 use reach when reported. If reach is missing, impressions may be used and are labeled. A measured zero reach remains n.a.']];
launch.getRange('I18').values=[['Completion rate = complete views / 3-second views, only when the platform supplies those exact counts and both belong to the same post/window.']];
launch.getRange('I19').values=[['Qualified demo visits need a tracked destination and a written qualification rule. Leave them blank until those are defined.']];
launch.getRange('I20').values=[['Do not add replies by the builder as independent demand. Record source exports and platform-specific definitions. No universal platform retention metric is assumed.']];

// Meaningful control and formula tests. Every temporary edit is restored.
const checks=[];function eq(label,actual,wanted){if(actual!==wanted)throw new Error(`${label}: ${actual} != ${wanted}`);checks.push({label,actual});}
eq('exactly fifteen primary file records',records.filter(r=>r[2]==='Primary').length,15);
eq('file-presence summary reconciles to scan',campaigns.getRange('B4').values[0][0],records.filter(r=>r[2]==='Primary'&&r[4]==='Present').length);
const savedAsset=assets.getRange('E9:E11').values;const savedTasks=tasks.getRange(`F9:F${endT}`).values;
assets.getRange('E9:E11').values=[['Present']];campaigns.getRange('D9').values=[['Approved']];
eq('approval alone cannot schedule',campaigns.getRange('E9').values[0][0],'Production open');
for(let i=0;i<taskRows.length;i++)if(taskRows[i][1]==='All')tasks.getRange(`F${9+i}`).values=[['Complete']];
eq('common tasks do not replace campaign review',campaigns.getRange('E9').values[0][0],'Production open');
for(let i=0;i<taskRows.length;i++)if(taskRows[i][1]==='C01'&&taskRows[i][2]==='Primary')tasks.getRange(`F${9+i}`).values=[['Complete']];
eq('optional founder shoot does not block primary letter',campaigns.getRange('E9').values[0][0],'Ready to schedule');
eq('other campaign tasks do not block this campaign',campaigns.getRange('E9').values[0][0],'Ready to schedule');
tasks.getRange('F9').values=[['Pending']];eq('reopened primary prerequisite stays visible',campaigns.getRange('E9').values[0][0],'Production open');
assets.getRange('E9').values=[['Missing']];eq('missing master file blocks readiness',campaigns.getRange('E9').values[0][0],'Assets open');
assets.getRange('E9:E11').values=savedAsset;tasks.getRange(`F9:F${endT}`).values=savedTasks;campaigns.getRange('D9').values=[['Pending']];
eq('unmeasured action rate stays blank',launch.getRange('U9').values[0][0],'');
launch.getRange('I9:L9').values=[[100,200,null,5]];eq('reach-based primary action rate',launch.getRange('U9').values[0][0],50);eq('rate basis labels reach',launch.getRange('V9').values[0][0],'Reach');
launch.getRange('I9').values=[[0]];eq('measured zero reach does not fall back',launch.getRange('U9').values[0][0],'n.a.');
launch.getRange('I9').values=[[null]];eq('missing reach uses stated impressions',launch.getRange('U9').values[0][0],25);eq('fallback basis is labeled',launch.getRange('V9').values[0][0],'Impressions');
launch.getRange('B9').values=[['C03']];launch.getRange('M9').values=[[8]];eq('campaign selector changes counted action to saves',launch.getRange('T9').values[0][0],8);
launch.getRange('P9:Q9').values=[[100,25]];eq('completion uses exact video-view denominator',launch.getRange('W9').values[0][0],0.25);
launch.getRange('B9').values=[['C01']];launch.getRange('I9:Q9').values=[Array(9).fill(null)];
wb.recalculate();
const errors=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:100},maxChars:2500});
const mainInspection=await wb.inspect({kind:'table',range:'Campaigns!A8:G13',include:'values,formulas',tableMaxRows:6,tableMaxCols:7,maxChars:4500});
const finalCounts={scannedAtUtc:scannedAt.toISOString(),primaryFilesPresent:campaigns.getRange('B4').values[0][0],approved:campaigns.getRange('E4').values[0][0],ready:campaigns.getRange('G4').values[0][0],assetFiles:records.length,sourceFiles:records.filter(r=>r[2]==='Source').length,taskRows:taskRows.length,openPrimaryTasks:tasks.getRange('B4').values[0][0],photoProof,graphicProof,photoComparison};
for(const [s,range,name] of [[campaigns,'A1:G23','campaigns'],[assets,'A1:F20','assets-main'],[assets,'G1:L6','assets-snapshot'],[assets,'G8:L20','assets-detail'],[tasks,'A1:H19','tasks-primary'],[tasks,'A20:K33','tasks-upgrades'],[launch,'A1:M13','launch-inputs'],[launch,'N8:W13','launch-rates'],[launch,'A16:G21','launch-definitions'],[launch,'I16:AA21','launch-rules']]){
 const image=await wb.render({sheetName:s.name,range,scale:1.5,format:'png'});await fs.writeFile(path.join(qa,`${name}.png`),new Uint8Array(await image.arrayBuffer()));
}
await fs.writeFile(path.join(qa,'verification.json'),JSON.stringify({finalCounts,checks,primaryValidation,errors:errors.ndjson,mainInspection:mainInspection.ndjson},null,2));
await fs.writeFile(path.join(root,'tracker','asset-register.json'),JSON.stringify(records.map(r=>({id:r[0],campaign:r[1],role:r[2],kind:r[3],state:r[4],path:r[5],evidence:r[6],seconds:r[8],width:r[9],height:r[10],bytes:r[11]})),null,2));
await (await SpreadsheetFile.exportXlsx(wb)).save(path.join(root,'production-tracker.xlsx'));
console.log(JSON.stringify({finalCounts,checks:checks.length,errors:errors.ndjson,output:path.join(root,'production-tracker.xlsx')},null,2));
