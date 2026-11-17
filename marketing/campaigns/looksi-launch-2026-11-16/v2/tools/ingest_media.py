"""Bind campaign media to original files and scoped product receipts."""
from pathlib import Path
import hashlib,json,shutil,subprocess
from PIL import Image,ImageOps,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[3]
MEDIA=ROOT/'media';(MEDIA/'archive').mkdir(parents=True,exist_ok=True);(MEDIA/'previews').mkdir(exist_ok=True)
records=[]
def add(src,rel,kind,scope,date,receipt=None):
    src=REPO/src;dst=MEDIA/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if src.resolve()!=dst.resolve():shutil.copy2(src,dst)
    entry=dict(path=str(dst.relative_to(ROOT)),source=str(src.relative_to(REPO)),sha256=hashlib.sha256(dst.read_bytes()).hexdigest(),kind=kind,scope=scope,date=date)
    if receipt:entry['receipt']=receipt
    records.append(entry);return dst

for stem in ['IMG_4870','IMG_4969','IMG_5071','IMG_5077']:
    add(Path('marketing/animated-trailer/public/media')/f'{stem}.jpg',f'sources/{stem}.jpg','camera JPEG preview','Original camera preview from the existing Nepal source project; not a Looksy edit.','historical source photo')
add(Path('marketing/campaigns/looksi-launch-2026-11-16/v2/media/exports/baseline/nepal-mountain-working-copy-full-resolution.png'),'exports/baseline/nepal-mountain-working-copy-full-resolution.png','native photo export','Unadjusted same-pipeline baseline; source is a 1800x1200 camera JPEG preview, not RAW.','2026-11-16')
add(Path('marketing/campaigns/looksi-launch-2026-11-16/v2/media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png'),'exports/finish/nepal-mountain-working-copy-full-resolution-2.png','native photo export','Verified treatment:5900K,+12%contrast,+0.15EV; combined endpoint only, no isolated-control or current recording claim.','2026-11-16')
for i in range(1,7):
    add(Path('.photo-editor/nepal-near-far/foreground-export')/f'nepal-near-far-{i:02d}.png',f'boards/nepal-near-far-{i:02d}.png','native board export','One of six 1920x1536 PNG exports from saved NEPAL / NEAR & FAR; export does not alone prove editable text authoring.','2026-11-16','media/boards/delivery-manifest.json')
add(Path('.photo-editor/nepal-near-far/foreground-export/NEPAL - NEAR & FAR-delivery-manifest-v1.json'),'boards/delivery-manifest.json','receipt','Native six-board export manifest.','2026-11-16')
native=Path('marketing/native-recreation-v4/polish-pass-2026-10-30/native-p26-opening-final')
vid=add(native/'p26-opening-final.mp4','archive/native-timeline-review-2026-11-02.mp4','native timeline export','Completed12s review excerpt,360frames1920x1080/30fps; motion design output, not live UI execution/full298s/current-build qualification. Its original audio is not used in revised creative.','2026-11-02','media/archive/native-timeline-render-receipt.json')
receipt=add(native/'native-render-receipt.json','archive/native-timeline-render-receipt.json','receipt','Historical native timeline output receipt; source hash independently checked.','2026-11-02')
expected=json.loads(receipt.read_text())['outputIntegrity']['sha256']
assert hashlib.sha256(vid.read_bytes()).hexdigest()==expected
for name in ['tone-curve','mask-exposure','poster-type','timeline-build']:
    add(Path('marketing/animated-trailer/public/media/desktop-action')/f'{name}.mp4',f'archive/{name}-2026-10-25.mp4','native screen recording','Archived development recording, cropped/resized in original source pipeline; no current build or task-time claim. Timeline capture provenance is weaker than tone-curve and must not substitute for export proof.','2026-10-25')
add(Path('marketing/animated-trailer/public/media/desktop-action/capture-provenance.json'),'archive/desktop-action-provenance.json','provenance','Original transformation notes for archived native recordings.','2026-10-25')
for record in records:
    path=ROOT/record['path']
    if path.suffix.lower() in ['.png','.jpg']:
        with Image.open(path) as im:
            record['dimensions']=list(im.size);record['embedded_icc']=bool(im.info.get('icc_profile'))
            name=path.stem
            if 'baseline/' in str(path):name='native-photo-baseline'
            if path.name=='nepal-mountain-working-copy-full-resolution-2.png':name='native-photo-finish'
            dest=MEDIA/'previews'/f'{name}.jpg'
            im=ImageOps.contain(im.convert('RGB'),(1920,1920));im.save(dest,quality=96,subsampling=0)
            record['layout_preview']=str(dest.relative_to(ROOT))

frames=MEDIA/'frames';frames.mkdir(exist_ok=True)
contacts=[]
for name in ['tone-curve','timeline-build']:
    src=MEDIA/'archive'/f'{name}-2026-10-25.mp4'
    for i,t in enumerate([0,5,10,15,20,25,30,35,40]):
        target=frames/f'{name}-{t:02d}.jpg'
        subprocess.run(['ffmpeg','-v','error','-y','-ss',str(t),'-i',str(src),'-frames:v','1','-q:v','2',str(target)],check=True)
        contacts.append((target,f'{name} / {t}s'))
for t in [0,1,2,4,7,10]:
    target=frames/f'native-timeline-output-{t:02d}.jpg'
    subprocess.run(['ffmpeg','-v','error','-y','-ss',str(t),'-i',str(vid),'-frames:v','1','-q:v','2',str(target)],check=True)
    contacts.append((target,f'Native export / {t}s'))
sheet=Image.new('RGB',(1920,1500),'#111111');d=ImageDraw.Draw(sheet)
for i,(path,label)in enumerate(contacts):
    x=(i%4)*480;y=(i//4)*250
    im=ImageOps.contain(Image.open(path),(470,215));sheet.paste(im,(x,y));d.text((x+8,y+220),label,fill='white')
sheet.save(ROOT/'qa/archive-footage-selection.jpg',quality=95)
manifest={'scope':'Local review material; no public posting. Existing source photographs and native project exports, plus archived recordings.','records':records,'excluded':['media/exports/finish/nepal-mountain-working-copy-full-resolution.png is identical to baseline and is not an after.'],'rights':'User project photography used for local campaign review. Confirm publication permissions for any recognizable people/property before external use. No competitor reference creative is included.','rendering_note':'Layout previews only resize/crop sources; no photographic treatment is simulated in Python. Native before/after pixels come from actual Looksy exports.'}
(MEDIA/'provenance.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps({'sources':len(records),'hashes_verified':True,'contact_sheet':'qa/archive-footage-selection.jpg'}))
