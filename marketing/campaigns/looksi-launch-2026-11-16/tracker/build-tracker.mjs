import fs from 'node:fs/promises';
import path from 'node:path';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const root = path.resolve(import.meta.dirname, '..');
const qaDir = path.join(root, 'tracker', 'qa');
await fs.mkdir(qaDir, { recursive: true });
const wb = Workbook.create();
const projects = wb.worksheets.add('Projects');
const production = wb.worksheets.add('Production tasks');
const launch = wb.worksheets.add('Launch log');
const colors = { ink:'#17191C', paper:'#F3F3F3', blue:'#3282FF', amber:'#FFF0CB', line:'#DFE3E8', red:'#FCE5E4' };

function base(sheet, lastCol, lastRow, title, widths) {
  sheet.showGridLines = false;
  sheet.tabColor = colors.ink;
  const used = sheet.getRange(`A1:${lastCol}${lastRow}`);
  used.format.font = { name:'Arial', size:10, color:colors.ink };
  used.format.verticalAlignment = 'center';
  used.format.rowHeight = 24;
  sheet.getRange('A2').values = [[title]];
  sheet.getRange('A2').format.font = { name:'Arial', size:16, bold:true };
  sheet.getRange(`A3:${lastCol}3`).format.borders = {bottom:{style:'thin',color:colors.line}};
  widths.forEach((width,i) => sheet.getRangeByIndexes(0,i,lastRow,1).format.columnWidth = width);
  sheet.freezePanes.freezeRows(8);
  sheet.freezePanes.freezeColumns(2);
}
function headers(sheet, range, values) {
  sheet.getRange(range).values = [values];
  sheet.getRange(range).format = {fill:colors.ink, font:{name:'Arial',size:10,bold:true,color:'#FFFFFF'},wrapText:true,horizontalAlignment:'center',verticalAlignment:'center',rowHeight:34};
}
function statusInput(range, values) {
  range.dataValidation = { rule:{type:'list', values} };
  range.format.fill = colors.amber;
}
function cond(range, formula) {
  range.conditionalFormats.addCustom(formula, {fill:colors.red,font:{color:'#8D2520',bold:true}});
}
function bandRows(sheet, start, end, lastCol) {
  for(let r=start;r<=end;r++) if(r%2===0) sheet.getRange(`A${r}:${lastCol}${r}`).format.fill='#F4F6F8';
}

const campaigns = [
  ['01','A note from the builder','Brady','Required for video','Record a candid founder take','Replace founder and demo slots','Own reason for building; no invented backstory'],
  ['02','Show the work','Editor','None','Record current photo and timeline workflow','Replace every product demo slot','Show actual controls and exported output'],
  ['03','Start with your eye','Editor','Optional voice','Record one beginner photo edit','Replace tutorial demo slots','Teach one repeatable edit, readable at phone size'],
  ['04','The final final club','Motion designer','None','Review joke and invitation','Approve type animation and timing','Pain is relatable; no unsupported product comparison'],
  ['05','A point of view','Motion designer','None','Review brand motion and message','Approve typography and motion','RGB light identity; creative intent, no feature proof'],
];
const tasks = [
 ['G01','All','Confirm campaign name','Brady','Pending','Choose LOOKSi casing for external posts','Campaign name approved without requiring app rename',0.25,'Current campaign brief; apps/desktop/qml/components/BrandWordmark.qml'],
 ['G02','All','Approve campaign claim language','Brady','Pending','Review captions, visuals and video text','Claims match current demonstrated build; intent wording retained',0.5,'campaign-briefs.md; copy.json'],
 ['G03','All','Choose and test destination','Brady','Pending','Set profile or feedback destination','Working destination matches the feedback CTA; no blank purchase link',0.5,'release/product.json: purchaseUrl and priceLabel blank'],
 ['G04','All','Approve media provenance','Producer','Pending','Review artwork and original audio provenance','Source and generation notes retained; original synthesized music provenance checked',0.5,'artwork/provenance.json; audio/original-pulse.wav; audio/provenance.json'],
 ['G05','All','Check platform and community rules','Producer','Pending','Recheck target community rules before posting','Posting allowed in selected community; founder affiliation disclosed',0.5,'research/release-style-research.md'],
 ['G06','All','Approve accessibility and export','Editor','Pending','Check phone preview and motion captions','Legible type, adequate contrast, alt text, full playback and correct framing',0.75,'boards/; images/; videos/'],
 ['F01','01','Film founder opening','Brady','Pending','Record 3 natural takes of the opening line','1080x1920 or larger; eye-level; clean voice; pause before/after line',0.5,'campaign-briefs.md: 01 video script'],
 ['F02','01','Film founder reason and invite','Brady','Pending','Explain your real reason and ask one question','Specific true motivation; no claims of guaranteed superiority',0.5,'copy.json: 01'],
 ['F03','01','Record product inserts','Editor','Pending','Capture one photo edit and one timeline action','Current build; owned media; no private filenames; export output visible',1,'marketing/media/captures/provenance.json: existing fixtures pending'],
 ['F04','01','Finish founder cut','Editor','Pending','Replace placeholders and add accurate captions','Founder speech and demo agree; no placeholder frames; full phone review',1.5,'videos/01-video.mp4; copy.json'],
 ['P01','02','Prepare real workflow media','Brady','Pending','Choose one owned shoot with still and video','Known source media; clear before view; useful result without staged UI',0.5,'Current owned source shoot to be selected'],
 ['P02','02','Capture photo workflow','Editor','Pending','Record selection, adjustment and output','Actual app controls and original output; no simulated timing claim',1,'apps/desktop/qml/workspaces/StudioWorkspace.qml'],
 ['P03','02','Capture video workflow','Editor','Pending','Record trim, grade and export','Actual timeline action and playable exported result',1,'apps/desktop/qml/workspaces/TimelineView.qml'],
 ['P04','02','Finish professional demo','Editor','Pending','Insert screen recordings and tool labels','Named actions visible; exact demonstrated result; no speed/competitor claims',1.5,'videos/02-video.mp4'],
 ['H01','03','Select tutorial image','Brady','Pending','Choose one owned image with clear learning goal','One understandable adjustment sequence; retain original for comparison',0.25,'Current owned photo to be selected'],
 ['H02','03','Record tutorial','Editor','Pending','Capture a single edit with clear cursor movement','Each instruction maps to a visible control; demonstrate saved output',1,'apps/desktop/qml/workspaces/StudioWorkspace.qml'],
 ['H03','03','Choose voice treatment','Brady','Pending','Choose text-only or record optional voice','No founder face required; text remains enough to understand steps',0.25,'campaign-briefs.md: 03 video script'],
 ['H04','03','Finish tutorial cut','Editor','Pending','Replace demo slots; time captions to actions','No placeholders; practical steps readable; before and after fairly labeled',1,'videos/03-video.mp4'],
 ['J01','04','Review joke and tone','Brady','Pending','Check joke feels like your own voice','No fake user quote, endorsement or implied verified time saving',0.25,'copy.json: 04'],
 ['J02','04','Finish animation','Motion designer','Pending','Review timing and text on phone','No founder filming needed; no UI placeholder or incorrect claim',0.5,'videos/04-video.mp4'],
 ['C01','05','Approve visual direction','Brady','Pending','Review RGB light and editorial typography','Existing RGB palette respected; campaign naming approved',0.25,'apps/desktop/qml/components/Theme.qml: brand RGB'],
 ['C02','05','Finish cinematic motion','Motion designer','Pending','Review pacing, crop and closing invite','No founder filming needed; no unintended product capability claim',0.5,'videos/05-video.mp4'],
 ['R01','Release','Confirm commercial offer','Brady','Pending','Finalize terms and any price before sales copy','commercialTermsApproved true; chosen pricing and checkout verified',1,'release/product.json: commercialTermsApproved false'],
 ['R02','Release','Verify release availability','Brady','Pending','Qualify distribution before saying available now','Supported release, installation and purchase journey confirmed',2,'docs/release-checklist.md; release/product.json'],
];

base(production,'J',37,'LOOKSi production tasks',[12,12,27,20,17,45,57,12,60,12]);
production.getRange('A4').values=[['Open tasks']];
production.getRange('B4').formulas=[['=COUNTA(A9:A32)-COUNTIF(E9:E32,"Complete")']];
production.getRange('D4').values=[['Plan hours']];
production.getRange('E4').formulas=[['=SUM(H9:H32)']];
production.getRange('A6').values=[['Hours are planning estimates. Complete all All-scope and campaign tasks before scheduling. Release-scope tasks apply before sales or availability claims.']];
production.getRange('A6').format.font.italic=true;
headers(production,'A8:J8',['Task ID','Scope','Work','Owner','Status','Next action','Acceptance criteria','Plan hours','Evidence / source','Priority']);
production.getRange('A9:J32').values=tasks.map(t=>[...t,t[1]==='05'?'P3':t[1]==='03'||t[1]==='04'?'P2':'P1']);
production.getRange('B9:B32').setNumberFormat('@');
bandRows(production,9,32,'J');
production.getRange('A9:I32').format.rowHeight=62;
production.getRange('C9:D32').format.wrapText=true;
production.getRange('F9:I32').format.wrapText=true;
production.getRange('H9:H32').setNumberFormat('0.00');
production.getRange('H9:H32').format.horizontalAlignment='right';
statusInput(production.getRange('E9:E32'),['Pending','In progress','Complete']);
cond(production.getRange('E9:E32'),'=E9<>"Complete"');
production.tables.add('A8:J32',true,'ProductionTasks').style='TableStyleLight1';

base(projects,'P',28,'LOOKSi campaign production tracker',[13,25,15,22,21,43,22,43,18,25,12,41,41,61,12,45]);
projects.getRange('A4').values=[['Deliverables']];projects.getRange('B4').formulas=[['=COUNTA(A9:A23)']];
projects.getRange('D4').values=[['Drafts delivered']];projects.getRange('E4').formulas=[['=COUNTIF(D9:D23,"Draft delivered")']];
projects.getRange('G4').values=[['Review approved']];projects.getRange('H4').formulas=[['=COUNTIF(I9:I23,"Approved")']];
projects.getRange('J4').values=[['Ready to schedule']];projects.getRange('K4').formulas=[['=COUNTIF(J9:J23,"Ready to schedule")']];
projects.getRange('A6').values=[['Amber review cells are editable. Videos 01–03 are footage animatics; 04–05 are motion review drafts. All videos are 18 seconds with original synthesized audio.']];
projects.getRange('A6').format.font.italic=true;
headers(projects,'A8:P8',['Asset ID','Campaign','Format','Draft delivery','Owner','Next action','Founder filming','Final production','Review','Launch state','Review hours','Export path','Editable source','Acceptance criteria','Priority','Dependencies']);
const projectRows=[];
for (const [id,name,owner,founder,next,final,accept] of campaigns) {
 for (const type of ['Board','Image','Video']) {
  const folder=type==='Board'?'boards':type==='Image'?'images':'videos';
  const suffix=type.toLowerCase();const ext=type==='Video'?'mp4':'png';
  const rel=`${folder}/${id}-${suffix}.${ext}`;
  let exists=true;try {await fs.access(path.join(root,rel));}catch{exists=false;}
  projectRows.push([`${id}-${type[0]}`,`${id} ${name}`,type,exists?'Draft delivered':'Render pending',type==='Video'?owner:'Designer',type==='Video'?next:'Review layout and headline',type==='Video'?founder:'None',type==='Video'?final:'Approve artwork, naming and caption','Pending',null,type==='Video'?0.5:0.25,rel,type==='Video'?`tools/build_creative.py; shot-timing.json`:`${folder}/${id}-${suffix}.svg`,type==='Video'?`18 seconds; 1080x1920; 24 fps; H264/AAC; original audio; ${accept}`:`${type==='Board'?'1920x1080 creative direction board':'1080x1350 social image'}; no clipped type; reviewed copy; source editable`,id==='01'||id==='02'?'P1':id==='05'?'P3':'P2',`G01–G06; ${id==='01'?'F01–F04':id==='02'?'P01–P04':id==='03'?'H01–H04':id==='04'?'J01–J02':'C01–C02'}`]);
 }
}
projects.getRange('A9:P23').values=projectRows;
bandRows(projects,9,23,'P');
projects.getRange('A9:N23').format.rowHeight=66;
projects.getRange('B9:H23').format.wrapText=true;
projects.getRange('L9:N23').format.wrapText=true;
projects.getRange('P9:P23').format.wrapText=true;
projects.getRange('K9:K23').setNumberFormat('0.00');
projects.getRange('K9:K23').format.horizontalAlignment='right';
statusInput(projects.getRange('I9:I23'),['Pending','Changes requested','Approved']);
for(let r=9;r<=23;r++) {
  projects.getRange(`J${r}`).formulas=[[`=IF(I${r}<>"Approved","Review open",IF(D${r}<>"Draft delivered","Render pending",IF(COUNTIFS('Production tasks'!$B$9:$B$32,"All",'Production tasks'!$E$9:$E$32,"<>Complete")+COUNTIFS('Production tasks'!$B$9:$B$32,LEFT(A${r},2),'Production tasks'!$E$9:$E$32,"<>Complete")>0,"Production open","Ready to schedule")))`]];
}
cond(projects.getRange('J9:J23'),'=J9<>"Ready to schedule"');
projects.tables.add('A8:P23',true,'CampaignProjects').style='TableStyleLight1';

base(launch,'W',26,'LOOKSi launch log',[13,28,18,18,27,16,16,17,16,17,17,17,17,52,19,29,17,21,20,16,46,46,18]);
launch.getRange('A4').values=[['Published posts']];launch.getRange('B4').formulas=[['=COUNT(C9:C13)']];
launch.getRange('D4').values=[['Campaigns logged']];launch.getRange('E4').formulas=[['=COUNTA(A9:A13)']];
launch.getRange('A6').values=[['Record a 7-day snapshot where practical. Compare the same platform, format, window and metric definition. Blank means not measured; zero means measured as zero.']];
launch.getRange('A6').format.font.italic=true;
headers(launch,'A8:W8',['Campaign ID','Campaign','Post date','Platform','Post URL','Impressions','3s views','Completions','Saves','Comments','Profile visits','3s view rate','Completion rate','Learning / next action','Selected format','Variant / source tag','Window days','Qualified demo visits','Useful feedback','Shares','Demo qualification rule','Metric source / definition','Snapshot date']);
launch.getRange('A9:W13').values=campaigns.map(([id,name])=>[id,name,...Array(21).fill(null)]);
launch.getRange('A9:A13').setNumberFormat('@');
bandRows(launch,9,13,'W');
launch.getRange('A9:W13').format.rowHeight=56;
launch.getRange('C9:K13').format.fill=colors.amber;
launch.getRange('N9:N13').format.fill=colors.amber;
launch.getRange('N9:N13').format.wrapText=true;
launch.getRange('O9:W13').format.fill=colors.amber;
launch.getRange('U9:V13').format.wrapText=true;
launch.getRange('C9:C13').setNumberFormat('mm/dd/yy');
launch.getRange('W9:W13').setNumberFormat('mm/dd/yy');
launch.getRange('F9:K13').setNumberFormat('#,##0');
launch.getRange('Q9:T13').setNumberFormat('#,##0');
launch.getRange('L9:M13').setNumberFormat('0.0%');
launch.getRange('F9:M13').format.horizontalAlignment='right';
statusInput(launch.getRange('D9:D13'),['Reddit','Instagram','YouTube Shorts','LinkedIn','Other']);
statusInput(launch.getRange('O9:O13'),['Text post','Image post','Carousel','Reel / Short','Other']);
launch.dataValidations.add({range:'F9:K13',rule:{type:'whole',operator:'greaterThanOrEqual',formula1:0}});
launch.dataValidations.add({range:'Q9:T13',rule:{type:'whole',operator:'greaterThanOrEqual',formula1:0}});
for(let r=9;r<=13;r++) {
 launch.getRange(`L${r}`).formulas=[[`=IF(AND(ISNUMBER(F${r}),ISNUMBER(G${r})),IF(F${r}=0,"n.a.",G${r}/F${r}),"")`]];
 launch.getRange(`M${r}`).formulas=[[`=IF(AND(ISNUMBER(G${r}),ISNUMBER(H${r})),IF(G${r}=0,"n.a.",H${r}/G${r}),"")`]];
}
launch.getRange('A16').values=[['Metric definitions']];launch.getRange('A16').format.font.bold=true;
launch.getRange('A17').values=[['3s view rate = 3-second video views / impressions. Completion rate = full video views / 3-second views.']];
launch.getRange('A18').values=[['Use only when the platform supplies these exact counts. Keep the metric blank if definitions or denominators differ.']];
launch.getRange('O16').values=[['Response metrics']];launch.getRange('O16').format.font.bold=true;
launch.getRange('O17').values=[['Qualified demo visits: unique tracked visits that meet the rule entered in column U. Leave blank until tracking and qualification are defined.']];
launch.getRange('O18').values=[['Useful feedback: distinct people naming a workflow, problem or specific request. Shares: platform-reported count. Record the source and counting differences in column V.']];
launch.getRange('O19').values=[['Source tag: channel and creative variant used in tracking. Window days: elapsed observation days. Snapshot date: date the actuals were recorded.']];
launch.getRange('A20').values=[['Primary question by campaign']];launch.getRange('A20').format.font.bold=true;
launch.getRange('A21:B25').values=[['01','Do relevant editors share a specific workflow problem?'],['02','Do professionals ask to see or test the demonstrated workflow?'],['03','Do beginners save the tutorial or ask a useful follow-up?'],['04','Does the joke earn relevant comments and profile visits?'],['05','Does the brand film earn relevant profile visits and interest?']];
launch.getRange('B21:B25').format.wrapText=true;launch.getRange('B21:B25').format.rowHeight=42;
launch.getRange('B21:B25').format.columnWidth=38;
launch.tables.add('A8:W13',true,'LaunchActuals').style='TableStyleLight1';

// Exercise the editable prerequisites and actuals. Restore every temporary input.
const checks=[];
function expect(label,actual,expected){if(actual!==expected)throw new Error(`${label}: ${actual} != ${expected}`);checks.push({label,actual});}
expect('project count', projects.getRange('B4').values[0][0],15);
const savedDraft=projects.getRange('D9').values[0][0];
projects.getRange('D9').values=[['Draft delivered']];
projects.getRange('I9').values=[['Approved']];
expect('review alone cannot schedule',projects.getRange('J9').values[0][0],'Production open');
production.getRange('E9:E14').values=[['Complete']];
expect('common tasks alone cannot schedule founder campaign',projects.getRange('J9').values[0][0],'Production open');
production.getRange('E15:E18').values=[['Complete']];
expect('all founder prerequisites complete',projects.getRange('J9').values[0][0],'Ready to schedule');
projects.getRange('D9').values=[['Render pending']];
expect('missing export cannot schedule',projects.getRange('J9').values[0][0],'Render pending');
projects.getRange('D9').values=[[savedDraft]];
projects.getRange('I9').values=[['Pending']];production.getRange('E9:E18').values=[['Pending']];
expect('blank metrics remain blank',launch.getRange('L9').values[0][0],'');
launch.getRange('F9:H9').values=[[100,50,25]];
expect('3s view rate responds',launch.getRange('L9').values[0][0],0.5);
expect('completion rate responds',launch.getRange('M9').values[0][0],0.5);
launch.getRange('F9:H9').values=[[0,0,0]];
expect('measured zero denominator unavailable',launch.getRange('L9').values[0][0],'n.a.');
launch.getRange('F9:H9').values=[[null,null,null]];
wb.recalculate();
const summary=await wb.inspect({kind:'region',sheetId:projects.name,range:'A4:K4',maxChars:2500,tableMaxRows:2,tableMaxCols:11});
const errors=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:100},maxChars:3000});
const finalCounts = {deliverables:projects.getRange('B4').values[0][0],draftsDelivered:projects.getRange('E4').values[0][0],approved:projects.getRange('H4').values[0][0],readyToSchedule:projects.getRange('K4').values[0][0],openProductionTasks:production.getRange('B4').values[0][0]};
await fs.writeFile(path.join(qaDir,'verification.json'),JSON.stringify({checks,finalCounts,summary:summary.ndjson,errors:errors.ndjson},null,2));
for(const [sheet,range,name] of [[projects,'A1:K16','projects-main'],[projects,'A17:K23','projects-later'],[projects,'L8:P16','projects-detail'],[production,'A1:H20','production-tasks'],[production,'A21:J32','production-detail'],[launch,'A1:N18','launch-log'],[launch,'O8:W19','launch-response'],[launch,'A20:C25','learning-questions']]){
 const image=await wb.render({sheetName:sheet.name,range,scale:1.5,format:'png'});
 await fs.writeFile(path.join(qaDir,`${name}.png`),new Uint8Array(await image.arrayBuffer()));
}
await (await SpreadsheetFile.exportXlsx(wb)).save(path.join(root,'production-tracker.xlsx'));
const csv=(matrix)=>matrix.map(row=>row.map(v=>'"'+String(v??'').replaceAll('"','""')+'"').join(',')).join('\n')+'\n';
await fs.writeFile(path.join(root,'production-tasks.csv'),csv([['Task ID','Scope','Work','Owner','Status','Next action','Acceptance criteria','Plan hours','Evidence / source','Priority'],...tasks.map(t=>[...t,t[1]==='05'?'P3':t[1]==='03'||t[1]==='04'?'P2':'P1'])]));
console.log(JSON.stringify({output:path.join(root,'production-tracker.xlsx'),projects:projectRows.length,tasks:tasks.length,checks,errors:errors.ndjson}));
