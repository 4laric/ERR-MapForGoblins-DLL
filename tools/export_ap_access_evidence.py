#!/usr/bin/env python3
"""Export typed flag evidence; never infer access from placement or proximity."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

JAR_NPCS = {44908320, 44908420}
JAR_FLAG = 1039449278
SCARAB_EVENTS = {90005300, 90005301}


def classify(row):
    """Classify known producer semantics, retaining incomplete flags as unknown."""
    flag = row.get('eventFlag', 0)
    defeat = row.get('defeatFlag', 0)
    evidence = []
    if flag:
        if row.get('npcParamId') in JAR_NPCS and flag == JAR_FLAG:
            evidence.append({'kind': 'negative_availability', 'flag': flag,
                             'available_when': False, 'basis': 'Jarburg story-despawn override'})
        elif row.get('emevdEventId') in SCARAB_EVENTS and defeat == flag:
            evidence.append({'kind': 'defeat', 'flag': flag,
                             'basis': 'scarab template hide-on-death override'})
        elif flag == JAR_FLAG:
            evidence.append({'kind': 'unknown', 'flag': flag,
                             'basis': 'possible Jarburg override; NPC identity absent or different'})
        else:
            evidence.append({'kind': 'collection_candidate', 'flag': flag,
                             'basis': 'producer normally reads lot getItemFlagId; original lot flag not independently supplied'})
    if defeat and not any(e['kind'] == 'defeat' and e['flag'] == defeat for e in evidence):
        evidence.append({'kind': 'defeat', 'flag': defeat, 'basis': 'explicit defeatFlag field'})
    return evidence


def build_report(rows, switches=(), puzzles=()):
    if not isinstance(rows, list) or not all(isinstance(r, dict) for r in rows):
        raise ValueError('items input must be a list of objects')
    by_part = {}
    for switch in switches:
        if type(switch.get('enabled_when_on')) is not bool or not isinstance(switch.get('flag'), int) or switch['flag'] <= 0:
            raise ValueError('switches require positive flag and boolean enabled_when_on')
        if not switch.get('map') or not switch.get('partName'):
            raise ValueError('switches require map and partName')
        by_part.setdefault((switch['map'], switch['partName']), []).append(switch)
    records = []
    for index, row in enumerate(rows):
        evidence = classify(row)
        for switch in by_part.get((row.get('map'), row.get('partName')), []):
            evidence.append({'kind': 'explicit_asset_availability', 'flag': switch['flag'],
                             'available_when': switch['enabled_when_on'],
                             'basis': 'supplied switch extraction, exact map/part join'})
        records.append({'reference_id': index, 'itemLotId': row.get('itemLotId'),
                        'lotSource': row.get('lotSource') if row.get('lotSource') in ('map', 'enemy') else None,
                        'map': row.get('map'), 'partName': row.get('partName'),
                        'raw_flags': {k: row[k] for k in ('eventFlag', 'defeatFlag', 'emevdEventId', 'npcParamId') if k in row},
                        'evidence': evidence, 'access_adjudicated': False})
    counts = Counter(e['kind'] for r in records for e in r['evidence'])
    return {'schema_version': 1, 'counts': {'item_rows': len(rows),
            'evidence_observations': dict(sorted(counts.items())),
            'rows_without_flag_evidence': sum(not r['evidence'] for r in records),
            'qualified_lot_rows': sum(r['lotSource'] is not None for r in records),
            'puzzle_groups': len(puzzles), 'supplied_switch_records': len(switches)},
            'records': records, 'puzzle_groups': puzzles,
            'limitations': ['Counts are input rows/evidence observations, not AP checks or distinct rules.',
                           'No prerequisite is inferred from proximity. Puzzle groups are not assigned to loot.',
                           'collection_candidate is not a verified acquisition flag; producer display overrides exist.',
                           'Missing switch or puzzle input means unavailable evidence, not absence of access conditions.',
                           'No check has been adjudicated free of additional access requirements.']}


def native_report(rows, placement=None):
    """Retain native display flags and existing identity candidates; no location join."""
    matches = {}
    for check in (placement or {}).get('checks', []):
        for comparison in check.get('comparisons', []):
            rid = str(comparison['reference_id'])
            matches.setdefault(rid, {})[check['ap_id']] = {
                'ap_id': check['ap_id'], 'name': check['name'],
                'identity_status': comparison.get('identity_status'),
                'placement_status': comparison.get('status')}
    records = []
    for row in rows:
        fields = row.get('param_fields', {})
        evidence = []
        for name, flag in fields.items():
            if not isinstance(flag, int) or flag <= 0:
                continue
            if name == 'eventFlagId' or name.startswith('textEnableFlag'):
                evidence.append({'kind': 'display_enable', 'field': name, 'flag': flag,
                                 'display_when': True})
            elif name.startswith('textDisableFlag'):
                evidence.append({'kind': 'display_hide', 'field': name, 'flag': flag,
                                 'display_when': False})
            elif name == 'clearedEventFlagId':
                evidence.append({'kind': 'display_cleared', 'field': name, 'flag': flag})
        rid = str(row['reference_id'])
        record = {k: row.get(k) for k in ('reference_id', 'itemLotId', 'lotSource', 'category',
                                         'map', 'x', 'y', 'z', 'coordinate_frame')}
        record.update(evidence=evidence, candidate_ap_checks=list(matches.get(rid, {}).values()),
                      access_adjudicated=False)
        if any(e['kind'] == 'display_enable' and e['flag'] == 3691 for e in evidence):
            record['source_interpretation'] = 'Flag 3691 is the Patches switched-chest positive availability gate in the pinned loot generator; display gate retained, no AP rule adjudicated.'
        records.append(record)
    enables = [r for r in records if any(e['kind'] == 'display_enable' for e in r['evidence'])]
    matched_enables = [r for r in enables if r['candidate_ap_checks']]
    return {'counts': {'native_pins': len(records),
                       'category_counts': dict(sorted(Counter(r['category'] for r in records).items())),
                       'pins_with_display_enable': len(enables),
                       'display_enable_pins_with_ap_candidates': len(matched_enables),
                       'distinct_ap_candidates_with_display_enable': len({c['ap_id'] for r in matched_enables for c in r['candidate_ap_checks']}),
                       'display_enable_flag_counts': dict(sorted(Counter(str(e['flag']) for r in enables for e in r['evidence'] if e['kind'] == 'display_enable').items()))},
            'records': records,
            'limitations': ['Display conditions are not automatically AP access requirements.',
                           'AP candidates reuse placement-report identity links; ambiguous candidates remain candidates.',
                           'Flag counts include repeated text slots. No prerequisite inferred from proximity.']}


def read_input(path):
    data = path.read_bytes()
    return json.loads(data), {'path': str(path.resolve()), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--items', type=Path, required=True)
    parser.add_argument('--switches', type=Path, help='List of map, partName, flag, enabled_when_on records from explicit asset-switch extraction')
    parser.add_argument('--seal-puzzles', type=Path)
    parser.add_argument('--native-reference', type=Path)
    parser.add_argument('--placement-report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inputs = {}
    rows, inputs['items'] = read_input(args.items)
    switches, puzzles = [], []
    if args.switches:
        switches, inputs['switches'] = read_input(args.switches)
    if args.seal_puzzles:
        puzzles, inputs['seal_puzzles'] = read_input(args.seal_puzzles)
    report = build_report(rows, switches, puzzles)
    if args.placement_report and not args.native_reference:
        parser.error('--placement-report requires --native-reference')
    if args.native_reference:
        native, inputs['native_reference'] = read_input(args.native_reference)
        placement = None
        if args.placement_report:
            placement, inputs['placement_report'] = read_input(args.placement_report)
        report['native'] = native_report(native, placement)
    report['inputs'] = inputs
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'items': report['counts'], 'native': report.get('native', {}).get('counts')}, indent=2))


if __name__ == '__main__':
    main()
