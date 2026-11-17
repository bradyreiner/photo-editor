"""Record observed native defects as editable, frame-anchored Looksy notes."""
import json
from pathlib import Path
import uuid
from native_review_session import call, save

HERE = Path(__file__).resolve().parent
notes = [
    ('intro-source', 0, 12, 'Intro hero presentation',
     'Native baseline screenshot at requested 5s shows a flat magenta panel where the reference has the hero photograph. Preview seek reported mediaReady=false; inspect native decode/source/mask readiness and recheck moving output before changing the creative opening. Observation: native-baseline/t00005.000.png. Exact decoded frame identity remains unverified.'),
    ('detail-layout', 43.5, 49.7, 'Keep pixel-edit image clear of copy',
     'The duplicate heading/body fade is corrected in the native edit, but the expanded boat detail image still covers the right ends of the single remaining heading/body at45.733s. Keep the photo expansion within the right column and preserve the selection grid. Compare native-baseline and native-raw-overlap-v1 captures. Verify the whole expansion, not just a held still.'),
    ('closing-source', 280, 298, 'Restore and review the closing sequence',
     'Native baseline screenshot at requested289s is almost black with a tiny white diamond, unlike the source reference plant/Gaussian scene. Inspect active clip geometry, opacity, source availability and preview/export parity. Native-baseline/t00289.000.png is an observed canvas state, not confirmed decoded frame identity.'),
]
existing = call('timeline.inspect_review_notes', {'state': 'all', 'maxItems': 256})
ids = {row.get('markerId') for row in existing['items']}
for slug, start, end, label, note in notes:
    marker_id = str(uuid.uuid5(uuid.NAMESPACE_URL, 'looksy:polish:2026-10-30:' + slug))
    result_path = HERE / ('note-' + slug + '-result.json')
    if marker_id in ids:
        print('Existing native note retained: ' + slug)
        continue
    intent_path = HERE / ('note-' + slug + '-intent.json')
    if intent_path.exists():
        raise RuntimeError('Inspect native note after uncertain prior invocation; do not replay.')
    values = {'markerId': marker_id, 'label': label, 'note': note,
              'startFrame': round(start * 30), 'endFrame': round(end * 30),
              'assignee': 'Trailer polish', 'approvalState': 'changes-requested'}
    save(intent_path, {'commandId': 'timeline.action.createReviewNote', 'values': values,
                       'document': call('timeline.inspect_document')})
    result = call('app.invoke_command', {'commandId': 'timeline.action.createReviewNote', 'values': values})
    save(result_path, result)
    print(json.dumps({'note': slug, 'result': result})[:450], flush=True)
save(HERE / 'notes-after-initial-review.json', call('timeline.inspect_review_notes', {'state': 'all', 'maxItems': 256}))
