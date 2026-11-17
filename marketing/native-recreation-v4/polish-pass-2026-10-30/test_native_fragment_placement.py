import copy
import unittest
from native_fragment_placement import validate_clip_references, verify_placement


def fixture():
    clip = {'clipId': 'photo', 'startSeconds': 62, 'trimStartSeconds': 0,
            'trimEndSeconds': 5, 'playbackRate': 1}
    tracks = [{'trackId': 'source', 'kind': 'video', 'visible': True,
               'locked': False, 'opacity': 1, 'clips': [clip]},
              {'trackId': 'destination', 'kind': 'video', 'visible': True,
               'locked': False, 'opacity': 1, 'clips': []}]
    doc = {'tracks': tracks}
    plan = {'steps': [{'id': 'move', 'commandId': 'timeline.action.setTimelineClipTiming',
                      'values': {'clipId': 'photo', 'trackId': 'destination',
                                 'startSeconds': 62, 'durationSeconds': 5}}]}
    return plan, doc, copy.deepcopy(doc)


class PlacementGuardTest(unittest.TestCase):
    def test_reuses_lane_with_unrelated_edits_outside_the_interval(self):
        plan, before, live = fixture()
        live['tracks'][1]['clips'].append({'clipId': 'earlier-edit', 'startSeconds': 22,
                                         'trimStartSeconds': 0, 'trimEndSeconds': 5})
        self.assertTrue(verify_placement(plan, before, live)['destinationIntervalsSimulated'])

    def test_rejects_target_moved_since_planning(self):
        plan, before, live = fixture()
        live['tracks'][1]['clips'] = live['tracks'][0]['clips']
        live['tracks'][0]['clips'] = []
        with self.assertRaisesRegex(ValueError, 'source track changed'):
            verify_placement(plan, before, live)

    def test_rejects_track_order_or_compositing_changes(self):
        for field, value in [('locked', True), ('visible', False), ('opacity', .3)]:
            plan, before, live = fixture()
            live['tracks'][1][field] = value
            with self.assertRaisesRegex(ValueError, 'compositing precondition'):
                verify_placement(plan, before, live)
        plan, before, live = fixture()
        live['tracks'].reverse()
        with self.assertRaisesRegex(ValueError, 'Track order changed'):
            verify_placement(plan, before, live)

    def test_rejects_new_destination_overlap(self):
        plan, before, live = fixture()
        live['tracks'][1]['clips'].append({'clipId': 'new-edit', 'startSeconds': 64,
                                         'trimStartSeconds': 0, 'trimEndSeconds': 2})
        with self.assertRaisesRegex(ValueError, 'Destination overlaps new-edit'):
            verify_placement(plan, before, live)

    def test_simulates_move_out_then_move_in_and_abutting_clips(self):
        plan, before, live = fixture()
        plan['steps'].append({'id': 'following', 'commandId': 'timeline.action.createTimelineTitle',
                             'values': {'trackId': 'destination', 'startSeconds': 67,
                                        'durationSeconds': 5}})
        self.assertTrue(verify_placement(plan, before, live)['destinationIntervalsSimulated'])
        plan['steps'][-1]['values']['startSeconds'] = 66.99
        with self.assertRaisesRegex(ValueError, 'Destination overlaps photo'):
            verify_placement(plan, before, live)

    def test_existing_ids_and_earlier_created_clip_references_are_distinct(self):
        plan, before, live = fixture()
        plan['steps'] = [
            {'id': 'new-title', 'commandId': 'timeline.action.createTimelineTitle',
             'values': {'trackId': 'destination', 'startSeconds': 70,
                        'durationSeconds': 2}},
            {'id': 'animate-new-title', 'commandId': 'timeline.action.setTimelineClipAnimation',
             'values': {'clipId': {'result': 'new-title', 'field': 'createdClipId'},
                        'channels': [{'parameter': 'opacity', 'keyframes': [
                            {'localSeconds': 0, 'value': 1, 'interpolation': 'linear'}]}]}},
            {'id': 'edit-existing-title', 'commandId': 'timeline.action.setTimelineClipTitle',
             'values': {'clipId': 'photo', 'values': {'text': 'Reviewed'}}},
        ]
        plan['placementGuards'] = {'targetClipIds': ['photo'],
                                   'existingTargetClipIds': ['photo']}
        targets, refs = validate_clip_references(plan)
        self.assertEqual(targets, {'photo'})
        self.assertEqual(refs, [('animate-new-title', 'new-title')])
        result = verify_placement(plan, before, live)
        self.assertEqual(result['targetSourceTracksVerified'], 1)
        self.assertEqual(result['createdClipReferencesValidated'], 1)
        plan['placementGuards']['existingTargetClipIds'] = ['another-clip']
        with self.assertRaisesRegex(ValueError, 'existingTargetClipIds'):
            validate_clip_references(plan)

    def test_rejects_malformed_unknown_forward_and_noncreate_references(self):
        bad_references = [
            {'result': 'new-title', 'field': 'trackId'},
            {'result': 'missing-title', 'field': 'createdClipId'},
            {'result': 'new-title', 'field': 'createdClipId', 'extra': True},
        ]
        for reference in bad_references:
            plan = {'steps': [
                {'id': 'new-title', 'commandId': 'timeline.action.createTimelineTitle',
                 'values': {'trackId': 'destination', 'startSeconds': 70,
                            'durationSeconds': 2}},
                {'id': 'use-title', 'commandId': 'timeline.action.setTimelineClipAnimation',
                 'values': {'clipId': reference}},
            ]}
            with self.assertRaises(ValueError):
                validate_clip_references(plan)

        forward = {'steps': [
            {'id': 'use-title', 'commandId': 'timeline.action.setTimelineClipAnimation',
             'values': {'clipId': {'result': 'new-title', 'field': 'createdClipId'}}},
            {'id': 'new-title', 'commandId': 'timeline.action.createTimelineTitle',
             'values': {'trackId': 'destination', 'startSeconds': 70,
                        'durationSeconds': 2}},
        ]}
        with self.assertRaisesRegex(ValueError, 'earlier create'):
            validate_clip_references(forward)

        noncreate = {'steps': [
            {'id': 'not-a-create', 'commandId': 'timeline.action.setTimelineClipTitle',
             'values': {'clipId': 'photo', 'values': {'text': 'x'}}},
            {'id': 'use-title', 'commandId': 'timeline.action.setTimelineClipAnimation',
             'values': {'clipId': {'result': 'not-a-create', 'field': 'createdClipId'}}},
        ]}
        with self.assertRaisesRegex(ValueError, 'native create operation'):
            validate_clip_references(noncreate)

    def test_simulates_timing_reference_to_created_clip_and_rejects_overlap(self):
        plan, before, live = fixture()
        create = {'id': 'new-title', 'commandId': 'timeline.action.createTimelineTitle',
                  'values': {'trackId': 'destination', 'startSeconds': 70,
                             'durationSeconds': 2}}
        move = {'id': 'move-new-title', 'commandId': 'timeline.action.setTimelineClipTiming',
                'values': {'clipId': {'result': 'new-title', 'field': 'createdClipId'},
                           'trackId': 'destination', 'startSeconds': 72,
                           'durationSeconds': 2}}
        second = {'id': 'second-title', 'commandId': 'timeline.action.createTimelineTitle',
                  'values': {'trackId': 'destination', 'startSeconds': 74,
                             'durationSeconds': 2}}
        plan['steps'] = [create, second, move]
        self.assertTrue(verify_placement(plan, before, live)['destinationIntervalsSimulated'])
        move['values']['startSeconds'] = 72.1
        with self.assertRaisesRegex(ValueError, 'Destination overlaps planned:second-title'):
            verify_placement(plan, before, live)


if __name__ == '__main__':
    unittest.main()
