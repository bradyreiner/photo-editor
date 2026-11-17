"""Pure placement guards for a reviewed native command fragment."""
import math

TRACK_FIELDS = ('kind', 'role', 'laneIndex', 'locked', 'visible', 'independent',
                'opacity', 'muted', 'solo', 'groupId', 'groupParentId',
                'groupVideoHidden', 'groupTransformScale', 'groupTransformOffsetX',
                'groupTransformOffsetY', 'groupTransformRotationDegrees')

CREATE_ACTIONS = {
    'timeline.action.createTimelineClip',
    'timeline.action.createTimelineTitle',
    'timeline.action.createTimelineGraphic',
    'timeline.action.createTimelineGenerator',
}


def validate_clip_references(plan):
    """Separate stable existing IDs from valid earlier create-result references.

    A new clip reference is deliberately limited to the native createdClipId
    result of an earlier create operation in this same plan. The authoring
    runner resolves these references only after that operation succeeds.
    """
    steps = plan.get('steps')
    if not isinstance(steps, list):
        raise ValueError('A native fragment must contain a steps array.')

    positions = {}
    creators = {}
    for index, step in enumerate(steps):
        if not isinstance(step, dict) or not isinstance(step.get('values'), dict):
            raise ValueError('Every native fragment step must have an object values field.')
        step_id = step.get('id')
        if not isinstance(step_id, str) or not step_id:
            raise ValueError('Every native fragment step needs a nonempty stable step ID.')
        if step_id in positions:
            raise ValueError('Duplicate native fragment step ID: ' + step_id)
        positions[step_id] = index
        if step.get('commandId') in CREATE_ACTIONS:
            creators[step_id] = index

    existing_target_ids = set()
    created_clip_references = []
    for index, step in enumerate(steps):
        values = step['values']
        if 'clipId' not in values:
            continue
        clip_id = values['clipId']
        if isinstance(clip_id, str):
            if not clip_id.strip():
                raise ValueError('A stable clipId must not be empty: ' + step['id'])
            existing_target_ids.add(clip_id)
            continue
        if not isinstance(clip_id, dict) or set(clip_id) != {'result', 'field'}:
            raise ValueError('A clipId result reference must contain exactly result and field: ' + step['id'])
        result_id = clip_id.get('result')
        field = clip_id.get('field')
        if not isinstance(result_id, str) or not result_id:
            raise ValueError('A clipId result reference needs a nonempty creator step ID: ' + step['id'])
        if field != 'createdClipId':
            raise ValueError('A clipId reference must request createdClipId: ' + step['id'])
        if result_id not in positions:
            raise ValueError('Unknown clip creator step: ' + result_id)
        if result_id not in creators:
            raise ValueError('A clip reference must point to a native create operation: ' + result_id)
        if creators[result_id] >= index:
            raise ValueError('A clip reference must point to an earlier create operation: ' + result_id)
        created_clip_references.append((step['id'], result_id))
    guards = plan.get('placementGuards', {})
    if guards is not None and not isinstance(guards, dict):
        raise ValueError('placementGuards must be an object when present.')
    if isinstance(guards, dict):
        for field in ('targetClipIds', 'existingTargetClipIds'):
            declared = guards.get(field)
            if declared is not None:
                if (not isinstance(declared, list) or
                        any(not isinstance(item, str) or not item for item in declared) or
                        len(set(declared)) != len(declared) or
                        set(declared) != existing_target_ids):
                    raise ValueError(field + ' must exactly list the existing stable clip targets.')
    return existing_target_ids, created_clip_references


def verify_placement(plan, baseline, live):
    target_ids, created_refs = validate_clip_references(plan)
    original = {t['trackId']: t for t in baseline['tracks']}
    current = {t['trackId']: t for t in live['tracks']}
    if list(original) != list(current):
        raise ValueError('Track order changed since planning; review compositing before binding.')
    membership = lambda doc: {c['clipId']: t['trackId']
                             for t in doc['tracks'] for c in t['clips']}
    before_membership, now_membership = membership(baseline), membership(live)
    for clip_id in target_ids:
        if clip_id not in before_membership or before_membership[clip_id] != now_membership.get(clip_id):
            raise ValueError('Target source track changed: ' + clip_id)

    used_tracks = {now_membership[i] for i in target_ids}
    for step in plan['steps']:
        track_id = step['values'].get('trackId')
        if isinstance(track_id, str):
            if track_id not in original or track_id not in current:
                raise ValueError('Planned track is unavailable: ' + track_id)
            used_tracks.add(track_id)
    for track_id in used_tracks:
        for field in TRACK_FIELDS:
            if original[track_id].get(field) != current[track_id].get(field):
                raise ValueError('Track compositing precondition changed: ' + track_id + '/' + field)

    # Check the actual operation intervals, rather than rejecting unrelated
    # additions elsewhere on a reusable lane. Simulate moves in command order.
    def play_duration(clip):
        return ((clip['trimEndSeconds'] - clip['trimStartSeconds']) /
                max(.25, clip.get('playbackRate', 1)))

    occupancy = {tid: {c['clipId']: (c['startSeconds'], play_duration(c))
                      for c in track['clips']} for tid, track in current.items()}
    creator_keys = {step['id']: 'planned:' + step['id']
                    for step in plan['steps']
                    if step.get('commandId') in CREATE_ACTIONS}
    for step in plan['steps']:
        values = step['values']
        track_id = values.get('trackId')
        if not isinstance(track_id, str):
            continue  # A newly created track is resolved and guarded natively.
        action = step['commandId'].removeprefix('timeline.action.')
        if action == 'setTimelineClipTiming':
            reference = values['clipId']
            clip_id = (creator_keys[reference['result']]
                       if isinstance(reference, dict) else reference)
            start, duration = values['startSeconds'], values['durationSeconds']
            for occupied in occupancy.values():
                occupied.pop(clip_id, None)
        elif action in {'createTimelineTitle', 'createTimelineGraphic',
                        'createTimelineGenerator', 'createTimelineClip'}:
            clip_id = 'planned:' + step['id']
            start = values['startSeconds']
            duration = (values.get('trimEndSeconds', values['durationSeconds']) -
                        values.get('trimStartSeconds', 0))
        else:
            continue
        if not math.isfinite(start) or not math.isfinite(duration) or duration <= 0:
            raise ValueError('Invalid placement interval: ' + step['id'])
        for other_id, (other_start, other_duration) in occupancy[track_id].items():
            if start < other_start + other_duration - 1e-6 and start + duration > other_start + 1e-6:
                raise ValueError('Destination overlaps ' + other_id + ': ' + step['id'])
        occupancy[track_id][clip_id] = (start, duration)
    return {'targetSourceTracksVerified': len(target_ids),
            'createdClipReferencesValidated': len(created_refs),
            'compositingTracksVerified': len(used_tracks),
            'destinationIntervalsSimulated': True, 'globalTrackOrderUnchanged': True}
