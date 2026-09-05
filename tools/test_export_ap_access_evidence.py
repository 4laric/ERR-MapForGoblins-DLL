import unittest
from export_ap_access_evidence import build_report, classify, native_report


class AccessEvidenceTests(unittest.TestCase):
    def test_native_display_gate_does_not_become_access_rule(self):
        rows = [{'reference_id': 'pin', 'category': 'LootUtilities', 'lotSource': 'map',
                 'param_fields': {'textEnableFlag2Id1': 3691, 'textDisableFlagId1': 123}}]
        placement = {'checks': [{'ap_id': 9, 'name': 'check', 'comparisons': [
            {'reference_id': 'pin', 'identity_status': 'single_candidate', 'status': 'agreement'}]}]}
        report = native_report(rows, placement)
        self.assertEqual(report['counts']['distinct_ap_candidates_with_display_enable'], 1)
        self.assertFalse(report['records'][0]['access_adjudicated'])
        self.assertEqual([e['kind'] for e in report['records'][0]['evidence']], ['display_enable', 'display_hide'])

    def test_native_no_enable_is_not_rule_free(self):
        report = native_report([{'reference_id': 'pin', 'category': 'WorldImpStatues',
                                 'param_fields': {'textDisableFlagId1': 1}}])
        self.assertEqual(report['counts']['pins_with_display_enable'], 0)
        self.assertFalse(report['records'][0]['access_adjudicated'])
        self.assertEqual(report['counts']['category_counts']['WorldImpStatues'], 1)

    def test_story_despawn_is_not_collection(self):
        evidence = classify({'eventFlag': 1039449278, 'npcParamId': 44908320})
        self.assertEqual(evidence[0]['kind'], 'negative_availability')
        self.assertFalse(evidence[0]['available_when'])

    def test_missing_identity_not_certified(self):
        self.assertEqual(classify({'eventFlag': 1039449278})[0]['kind'], 'unknown')

    def test_scarab_death_is_not_collection(self):
        evidence = classify({'eventFlag': 123, 'defeatFlag': 123, 'emevdEventId': 90005300})
        self.assertEqual([e['kind'] for e in evidence], ['defeat'])

    def test_collection_and_defeat_remain_separate(self):
        evidence = classify({'eventFlag': 123, 'defeatFlag': 456})
        self.assertEqual([e['kind'] for e in evidence], ['collection_candidate', 'defeat'])

    def test_switches_preserve_both_polarities_and_exact_part(self):
        rows = [{'map': 'm31', 'partName': part, 'itemLotId': 7} for part in ('cloth', 'glass', 'other')]
        switches = [{'map': 'm31', 'partName': part, 'flag': 3691, 'enabled_when_on': state}
                    for part, state in [('cloth', False), ('glass', True)]]
        report = build_report(rows, switches)
        self.assertFalse(report['records'][0]['evidence'][0]['available_when'])
        self.assertTrue(report['records'][1]['evidence'][0]['available_when'])
        self.assertEqual(report['records'][2]['evidence'], [])
        self.assertTrue(all(r['lotSource'] is None for r in report['records']))
        self.assertTrue(all(not r['access_adjudicated'] for r in report['records']))

    def test_invalid_polarity_rejected(self):
        with self.assertRaises(ValueError):
            build_report([], [{'flag': 1, 'enabled_when_on': 'false'}])

    def test_puzzle_not_joined_by_proximity(self):
        report = build_report([{'x': 1, 'y': 2}], puzzles=[{'door_part': {'x': 1, 'y': 2}}])
        self.assertEqual(report['records'][0]['evidence'], [])
        self.assertEqual(report['counts']['puzzle_groups'], 1)


if __name__ == '__main__':
    unittest.main()
