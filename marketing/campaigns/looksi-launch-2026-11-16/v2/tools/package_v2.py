"""Package final review creative and sources, excluding rebuild/decoded caches."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,zipfile,re
from pypdf import PdfReader
ROOT=Path(__file__).resolve().parents[1]
START=datetime.fromisoformat('2026-11-16T22:07:49+00:00');NOW=datetime.now(timezone.utc)
elapsed=(NOW-START).total_seconds();assert elapsed>=3600,elapsed
verify=json.loads((ROOT/'qa/final-verification.json').read_text());tracker=json.loads((ROOT/'tracker/qa/verification.json').read_text());research=json.loads((ROOT/'research/research-v2.json').read_text())
assert verify['primary_campaigns']==5 and verify['core_deliverables']==15 and verify['carousel_slides']==31
assert len(verify['videos'])==30 and all(v['decode']=='passed'for v in verify['videos'])
assert tracker['finalCounts']['primaryFilesPresent']==15 and tracker['finalCounts']['approved']==0 and tracker['finalCounts']['ready']==0
assert len(research['cases'])==20 and len(research['styles'])==5
assert len(PdfReader(ROOT/'creative-direction-book.pdf').pages)==20
bookqa=json.loads((ROOT/'directorbook/qa/verification.json').read_text())
ledger={'rework_started_at_utc':START.isoformat(),'packaged_at_utc':NOW.isoformat(),'elapsed_wall_seconds':round(elapsed),'elapsed_wall_minutes':round(elapsed/60,1),'timing_note':'Actual elapsed rework time; not aggregate agent minutes. No artificial waiting was used to satisfy the requested minimum.',
 'workstreams':['Primary-release research and audience objections','Current native photo import, committed edits, actual baseline/chosen export and pixel verification','Five distinct motion/graphic executions with hook alternatives and actual carousel artwork','Independent creative reviews and source-specific revisions','20-page designed director book and rendered-page QA','Editable production tracking, native proof ledger, color/audio/vector finishing and portable delivery'],
 'native_app_final_state':'Existing Looksy PID20532 verified running at23:09UTC. Window inaccessible after Timeline setup; process was not forcibly closed or restarted. Separate workshop copy used; earlier Nepal boards untouched.'}
(ROOT/'qa/work-log.json').write_text(json.dumps(ledger,indent=2))
audit={'objective':'Substantially improve the entire Looksi campaign program through at least one hour of work, preserving five styles, boards/images/videos and production tracking.',
 'requirements':[
 {'requirement':'At least one hour of substantive rework','evidence':'qa/work-log.json','status':'proved','elapsed_minutes':ledger['elapsed_wall_minutes']},
 {'requirement':'Five researched styles','evidence':['research/research-v2.md','research/research-v2.json'],'status':'proved','cases':20,'styles':5},
 {'requirement':'Five boards, images and videos','evidence':['campaigns.json','qa/final-verification.json'],'status':'proved','primary_deliverables':15,'hero_films':5,'alternate_openings':10,'silent_masters':15,'carousel_slides':31},
 {'requirement':'More distinctive and polished executions','evidence':['creative-direction-book.pdf','qa/creative-review-round1.md','qa/creative-review-round2.md','qa/resolved-review-notes.md','qa/five-directions-v2.jpg'],'status':'proved as rendered creative review deliverables; human taste approval remains open'},
 {'requirement':'Genuine product work and accurate claims','evidence':['media/provenance.json','qa/final-verification.json','strategy.md'],'status':'proved for fresh same-source native photo exports and dated native board/Timeline artifacts; current continuous workflow is not claimed'},
 {'requirement':'Production tracking and founder requirements','evidence':['production-tracker.xlsx','campaign01/self-shoot-director.md','planning/'],'status':'proved','human_approvals':'Pending','filming':'Optional separate production versions; no primary founder placeholder'},
 {'requirement':'Reviewable professional handoff','evidence':['index.html','creative-direction-book.pdf','copybook.md','START-HERE.txt'],'status':'proved','limitations':'Local HTML browser automation unavailable; actual PDF/image layouts and encoded films inspected directly.'}],
 'publication':'None. No outreach, posting, spending, product rename or commercial-offer change.'}
(ROOT/'DELIVERY.json').write_text(json.dumps(audit,indent=2))
# Plain-language spacing in the handoff note, without altering real file paths.
p=ROOT/'START-HERE.txt';s=p.read_text()
for a,b in[('/16November2026','/ 16 November 2026'),('a20-page','a 20-page'),('run32/','run 32/'),('36seconds','36 seconds'),('giving15full','giving 15 full'),('with15matching','with 15 matching'),(',31slides',', 31 slides'),('covers20specific','covers 20 specific'),('from16November','from 16 November'),('1800x1200camera','1800 x 1200 camera'),('treatment:5900K,+12%contrast,+0.15EV','treatment: 5900 K, +12% contrast, +0.15 EV'),('six1920x1536board','six 1920 x 1536 board'),('hash-verified12-second','hash-verified 12-second'),('dated25October','dated 25 October'),('AAC48kHz','AAC 48 kHz'),('all30final','all 30 final'),(',31slides',', 31 slides'),('and20PDF','and 20 PDF')]:s=s.replace(a,b)
p.write_text(s)
dest=ROOT.parents[2]/'outputs/looksi-campaign-studio-v2-2026-11-16.zip';dest.parent.mkdir(exist_ok=True)
skip_dirs={'node_modules','__pycache__','round2','curve-frames','motion-frames','frames'}
paths=[]
for p in sorted(ROOT.rglob('*')):
    if p.is_symlink()or not p.is_file():continue
    rel=p.relative_to(ROOT)
    if any(x in skip_dirs for x in rel.parts)or p.suffix=='.zip'or p.name.endswith('.inspect.ndjson'):continue
    if 'qa'in rel.parts and p.suffix in['.mp4','.mov']:continue
    paths.append((p,Path('looksi-campaign-studio-v2')/rel))
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
    for p,arc in paths:z.write(p,arc)
with zipfile.ZipFile(dest)as z:
    assert z.testzip()is None
    names=set(z.namelist())
    for c in json.loads((ROOT/'campaigns.json').read_text())['campaigns']:
        for p in c['assets'].values():assert'looksi-campaign-studio-v2/'+p in names,p
    for p in['creative-direction-book.pdf','production-tracker.xlsx','copybook.md','index.html']:assert'looksi-campaign-studio-v2/'+p in names
receipt={'zip':str(dest),'files':len(paths),'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'integrity':'passed','elapsed_rework_minutes_at_packaging':ledger['elapsed_wall_minutes'],'pdf_sha256':hashlib.sha256((ROOT/'creative-direction-book.pdf').read_bytes()).hexdigest(),'tracker_sha256':hashlib.sha256((ROOT/'production-tracker.xlsx').read_bytes()).hexdigest()}
(ROOT/'qa/package-receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt))
