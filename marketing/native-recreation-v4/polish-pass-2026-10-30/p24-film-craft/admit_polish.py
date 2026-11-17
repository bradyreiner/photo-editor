import ctypes,hashlib,json,os,sys
from pathlib import Path
P=Path(__file__).resolve().parent;sys.path.insert(0,str(P.parent))
from native_review_session import WORKER,call,save
assert not (P/'admission-intent.json').exists(), 'Inspect prior admission; never replay.'
doc=call('timeline.inspect_document');assert doc['active'] and doc['ready']
assert not call('timeline.inspect_task')['active']
source=Path(WORKER['data'])/'timelines/documents'/f"{WORKER['timelineId']}.json"
backup=P/'before-native.json';assert not backup.exists()
lib=ctypes.CDLL('/usr/lib/libSystem.B.dylib',use_errno=True)
lib.clonefile.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int];lib.clonefile.restype=ctypes.c_int
assert lib.clonefile(os.fsencode(source),os.fsencode(backup),0)==0
sha=lambda x:hashlib.file_digest(x.open('rb'),'sha256').hexdigest()
assert sha(source)==sha(backup)
import subprocess
h=json.loads(subprocess.check_output(['/private/tmp/looksy-dense-native-hash',str(backup)],text=True))['documentHash']
assert h==doc['documentHash']==call('timeline.inspect_document')['documentHash']
values={'targetResourceId':doc['resourceId'],'objective':'Polish the existing Looksy trailer in place: fluid readable opening, connected chapter handoffs, coherent split-carousel motion and stronger image-visible feature demonstrations. Preserve chapter order, original media, current brand typography and the continuous New Gods soundtrack.',
'actions':[{'kind':'framing','summary':'Refine existing camera, image, mask and layer animation for continuous transitions and clean settled poses.'},{'kind':'text','summary':'Improve readable title entrances, hierarchy and timing without losing existing feature detail.'},{'kind':'effects','summary':'Make existing image-visible effect and mask demonstrations coherent with their controls.'},{'kind':'delivery','summary':'Save an editable native checkpoint, compare exact authored changes, and review native rendered excerpts.'}],
'assetPaths':[],'outputDirectory':str((P.parent/'native-projects').resolve()),'confirmed':True}
save(P/'admission-intent.json',{'document':doc,'backup':str(backup),'backupSha256':sha(backup),'values':values})
r=call('timeline.admit_task',values);save(P/'admission-result.json',r);print(json.dumps(r)[:1800])
