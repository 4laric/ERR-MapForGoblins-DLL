# Offline AP access evidence export

Run `python tools/export_ap_access_evidence.py --items PATH/items_database.json --output PATH/access-evidence.json`.
Optional `--seal-puzzles PATH` preserves extracted puzzle groups without associating them to loot. Optional `--switches PATH` accepts a list of objects with `map`, `partName`, positive integer `flag`, and boolean `enabled_when_on`. Supply actual EMEVD asset-switch extraction, not guessed gates. Both flag polarities are preserved; matching uses exact map and part name, never proximity.

The report records SHA-256 of every supplied file, original zero-based item row `reference_id`, qualified lot source only when explicitly present, raw flags and evidence observations. Collection candidates are not certified acquisition flags. Unknown remains unknown. No row is adjudicated free of additional requirements. Counts measure database rows/evidence observations, not distinct AP checks, pins, or rules.

## Vanilla bundle run

The downloaded vanilla item database contained 26,778 rows. This export found 4,752 collection-candidate observations, 458 defeat observations and 6 negative-availability observations (Jarburg story-despawn overrides). 21,828 rows contain no classified flag evidence. Defeat/collection observations can coexist on a row. Lot table identity is absent in this older input, hence zero explicitly qualified lot rows. No seal/switch input was supplied; zero supplied groups means missing evidence, not no puzzle/key/quest rules.

Run tests with `python -m unittest discover -s tools -p test_export_ap_access_evidence.py -v` (7 passing on this bundle audit).

## Additional source-grounded candidates

Examined fork e3aa511728c7be7ae2627f4586bcd45d2a603dd0 and upstream VirusAlex a25443312dd07c21bb616bd2aeda16ee889df045; the cited generator files have identical git blob SHAs at those pins.

- [Patches switched chests](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/switched_chests.py#L54): actual asset availability on flag 3691. Cloth is OFF, Glass Shard ON. Current marker output deliberately drops the OFF gate; extract before that filtering to preserve both sides. This report does not fabricate records from those comments.
- [Seal-puzzle groups](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/extract_seal_puzzles.py#L139): door entity, group and activation flags are exportable; full controller logic and loot-to-gated-space association are not supplied.
- [Imp seals](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/generate_imp_statues.py#L56): position and Stonesword/Imbued Sword Key type, not per-check membership or key quantity.
- [Painting extraction](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/generate_paintings.py#L84): painting collection flags; this generator does not retain a reward-prerequisite relation.
- [Jarburg override](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/extract_all_items.py#L1367): flags NPC rows 44908320/44908420 with 1039449278 to hide after village massacre. Treat as negative availability, never acquired-item evidence.
- [Scarab override](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/extract_all_items.py#L1230): event 90005300/90005301 uses death flag when lot acquisition flag would not fire.

The exporter needs no game installation, pythonnet or live process. It only reads the paths explicitly supplied and writes the requested report.

## Native pin display conditions

Add `--native-reference ../inputs/native_reference.json --placement-report ../reports/placement.json` to include native param flag evidence and existing placement identity candidates. Inputs and placement report are SHA-256 recorded. The exporter never reassigns checks by coordinate proximity. Reused ambiguous identity links remain candidate links.

The actual 7,031 native pins contain **one pin with a positive display-enable condition**: native row 5500008, map lot 31000030, category LootUtilities, Murkwater Cave Glass Shard. Flag 3691 appears in two text slots on this one pin. Its single AP identity candidate is 7772114, `Limgrave :: Glass Shard - treasure · Murkwater Cave [f31007030]`. Its collection/display-hide flag is separately retained as 31007030. The pinned switch generator explains 3691 as chest availability; this report does not assert an implemented or missing AP rule.

There are also **38 WorldImpStatues pins**, 37 WorldInteractables, 10 WorldPaintings and 62 WorldSpiritSprings. These provide positioned gate/puzzle-related objects for review; no nearby check is assigned a prerequisite from these counts. Absence of a positive display-enable condition on other item pins does not establish access-rule completeness.

Param semantics: [eventFlagId map opening and textEnableFlagId text display](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/src/from/paramdef/WORLD_MAP_POINT_PARAM_ST.hpp#L28); [Patches group-two enable emission](https://github.com/4laric/ERR-MapForGoblins-DLL/blob/e3aa511728c7be7ae2627f4586bcd45d2a603dd0/tools/generate_loot_massedit.py#L875). All are exported as display evidence first. Tests now total 9.
