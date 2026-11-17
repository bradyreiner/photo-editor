import subprocess
from pathlib import Path
P=Path(__file__).resolve().parent.parent
for label,start,end,frames in [('p26-scrub-after',5925,5955,[5925,5929,5931,5937,5940,5950]),('p26-opening-final',0,360,[0,15,30,45,60,120,249,255,260,267,285,359])]:
 assert not (P/f'excerpt-{label}-intent.json').exists(), 'Inspect retained receipt; never resubmit.'
 subprocess.run(['python3',str(P/'submit_native_review.py'),'--label',label,'--start-frame',str(start),'--end-frame',str(end)],check=True)
 subprocess.run(['python3',str(P/'watch_native_review.py'),'--label',label],check=True)
 args=['python3',str(P/'collect_native_review.py'),'--label',label]
 for frame in frames:args+=['--sample-frame',str(frame)]
 subprocess.run(args,check=True)
 print('Verified final native excerpt:',label,flush=True)
