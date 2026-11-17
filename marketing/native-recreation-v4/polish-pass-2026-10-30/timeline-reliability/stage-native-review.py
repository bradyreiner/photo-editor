import hashlib,json,plistlib,subprocess,tempfile,sys
from pathlib import Path
here=Path(__file__).resolve().parents[1]
label=sys.argv[1]; assert label.replace('-', '').isalnum()
receipt=here/('release-build-'+label+'.json'); assert not receipt.exists()
previous=json.loads((here/'release-build-static-clock-native.json').read_text())
source=Path('/private/tmp/looksy-timeline-reliability-build/apps/desktop/Looksy.app')
root=Path(tempfile.mkdtemp(prefix='looksy-'+label+'-',dir='/private/tmp'))
app=root/'Looksy Review.app'
subprocess.run(['/usr/bin/ditto',str(source),str(app)],check=True)
frameworks=app/'Contents/Frameworks'; frameworks.mkdir(exist_ok=True)
for entry in (Path(previous['app'])/'Contents/Frameworks').iterdir():
 if not (frameworks/entry.name).exists(): subprocess.run(['/usr/bin/ditto',str(entry),str(frameworks/entry.name)],check=True)
p=app/'Contents/Info.plist'
info=plistlib.loads(p.read_bytes()); info.update(CFBundleIdentifier='com.virel.studio.timeline-review',CFBundleName='Looksy Timeline Review',CFBundleDisplayName='Looksy Timeline Review');p.write_bytes(plistlib.dumps(info))
subprocess.run(['/usr/bin/codesign','--force','--deep','--sign','-',str(app)],check=True)
binary=app/'Contents/MacOS/Looksy'
sha=lambda f:hashlib.file_digest(f.open('rb'),'sha256').hexdigest()
files=['apps/desktop/qml/workspaces/TimelineView.qml','apps/desktop/qml/components/TimelineEffectsPanel.qml','apps/desktop/src/timeline_document_controller.cpp','apps/desktop/src/timeline_edit_journal_codec.cpp']
record=dict(previous,app=str(app),binarySha256=sha(binary),previousReceipt=str(here/'release-build-static-clock-native.json'),buildLog='/private/tmp/looksy-timeline-empty-cache-build.log',regressionLog='/private/tmp/looksy-timeline-empty-cache-tests.log',sourceHashes={f:sha(Path(f)) for f in files},scope='Internal native timeline review: static-clock guards, lazy absent-cache UI and persistent-vs-runtime source verification; concurrent unrelated work preserved, not global release qualification.')
receipt.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({'app':str(app),'binarySha256':record['binarySha256']}))
