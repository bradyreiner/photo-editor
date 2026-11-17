from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[1];paths=[]
for d in ['src/studio-film','src/app-kit','public/media/studio','public/brand']:
 p=r/d
 if p.exists():paths += [x for x in p.rglob('*') if x.is_file()]
paths += list((r/'public/media').glob('*.jpg'))
paths += [r/p for p in ['src/Root.tsx','src/index.ts','public/media/looksy-logo-dark.png','public/audio/studio/master.wav','public/media/refine/arrival.mp4','public/media/refine/hike.mp4','public/media/refine/night.mp4','public/media/launch-1268.mp4','public/media/MVI_5051-2.3-5.9.mp4']]
assert all(p.exists() for p in paths)
manifest={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(paths))}
(r/'out/studio/render-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Frozen {len(manifest)} source and media inputs',flush=True)
