#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import zipfile

ROOT=Path(__file__).resolve().parents[1]
audit={
 'objective':'Five research-informed Looksi campaigns with boards, images, videos and production tracking.',
 'scope':'Creative campaign package, including rendered samples and planned filming requirements; no public publishing or founder performance is claimed.',
 'requirements':[
  {'requirement':'Five differentiated tonalities researched from actual releases','evidence':['research/release-style-research.md','research/research.json'],'result':'Five aligned styles, two primary release references per style, native social companion and platform notes. Instagram execution explicitly identified as adaptation.'},
  {'requirement':'One board, image and video per style','evidence':['qa/package-verification.json','boards/','images/','videos/'],'result':'5 PNG creative boards, 5 PNG social images, 5 H264/AAC 18-second videos; 10 self-contained editable SVGs.'},
  {'requirement':'Maker message for professionals and hobbyists in photo/video','evidence':['copy.json','campaign-briefs.md','shot-timing.json'],'result':'Five matching caption sets, founder affiliation, photo/video positioning, accurate development status; rendered copy and timings agree.'},
  {'requirement':'Founder filming versus animation requirements','evidence':['production-tracker.xlsx','production-tasks.csv','production-playbook.html'],'result':'01 requires founder; 01–03 require real app capture for final filmed versions; 03 voice optional; 04–05 require no filming. All human review and future capture tasks remain pending.'},
  {'requirement':'Agency-level project planning','evidence':['production-tracker.xlsx','production-playbook.html','campaign-briefs.md'],'result':'15 deliverables, 24 tasks, owners, dependencies, planning estimates, review/launch formulas, blank actuals log, audience roles, two-week sequence, testing hypotheses and provenance.'},
  {'requirement':'Reviewable artifacts and verification','evidence':['index.html','qa/package-verification.json','tracker/qa/verification.json'],'result':'All 5 MP4s fully decode; all raster dimensions and editable SVG XML verified; 57 local HTML links resolve; 9 workbook recalculation checks pass, zero formula errors. Visual asset contact sheets reviewed directly. Local HTML browser rendering was blocked by protocol policy and is not claimed.'}
 ],
 'handoff':'Creative package complete. Filming and final creative approvals are intentionally tracked future production steps, as requested.',
 'application_changes':'None; only this new campaign directory and a delivery ZIP were created.'
}
(ROOT/'DELIVERY.json').write_text(json.dumps(audit,indent=2))
dest=ROOT.parents[1]/'outputs/looksi-launch-campaigns-2026-11-16.zip'
dest.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(dest,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for path in sorted(ROOT.rglob('*')):
  if path.is_symlink() or not path.is_file():continue
  rel=path.relative_to(ROOT)
  if 'node_modules' in rel.parts or '__pycache__' in rel.parts or path.name.endswith('.inspect.ndjson'):continue
  z.write(path,Path(ROOT.name)/rel)
with zipfile.ZipFile(dest) as z:
 assert z.testzip() is None
 for id in ['01','02','03','04','05']:
  for folder,name in [('boards',f'{id}-board.png'),('images',f'{id}-image.png'),('videos',f'{id}-video.mp4')]:
   assert f'{ROOT.name}/{folder}/{name}' in z.namelist()
 assert f'{ROOT.name}/production-tracker.xlsx' in z.namelist()
 print(json.dumps({'zip':str(dest),'files':len(z.namelist()),'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'zip_integrity':'passed'}))
