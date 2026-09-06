import unittest

from lot_identity import linkage_lot_type, resolve_lot_source


class LotIdentityTests(unittest.TestCase):
    def test_enemy_placement_can_bind_map_table_despite_collision(self):
        record = {'itemLotId': 123, 'source': 'enemy', 'lotSource': 'map'}
        table = resolve_lot_source(record, {123: 'map reward'}, {123: 'other drop'})
        self.assertEqual(table, 'map')
        self.assertEqual(linkage_lot_type(record), 1)

    def test_explicit_enemy_binding_survives_collision(self):
        record = {'itemLotId': 123, 'source': 'emevd', 'lotSource': 'enemy'}
        self.assertEqual(resolve_lot_source(record, {123: {}}, {123: {}}), 'enemy')
        self.assertEqual(linkage_lot_type(record), 2)

    def test_missing_explicit_row_never_uses_other_table(self):
        record = {'itemLotId': 123, 'lotSource': 'map'}
        self.assertIsNone(resolve_lot_source(record, {}, {123: {}}))

    def test_unqualified_event_resolves_only_unique_membership(self):
        record = {'itemLotId': 123, 'source': 'emevd'}
        self.assertEqual(resolve_lot_source(record, {123: {}}, {}), 'map')
        self.assertEqual(resolve_lot_source(record, {}, {123: {}}), 'enemy')
        self.assertIsNone(resolve_lot_source(record, {}, {}))
        with self.assertRaisesRegex(ValueError, 'Ambiguous lot'):
            resolve_lot_source(record, {123: {}}, {123: {}})

    def test_legacy_display_source_cannot_authorize_table_linkage(self):
        for source in ('treasure', 'enemy', 'emevd', 'emevd_treasure'):
            with self.subTest(source=source):
                with self.assertRaisesRegex(ValueError, 'rerun extract_all_items'):
                    linkage_lot_type({'itemLotId': 123, 'source': source})

    def test_invalid_explicit_table_is_rejected(self):
        for table in ('other', 1, ''):
            with self.subTest(table=table):
                with self.assertRaises(ValueError):
                    resolve_lot_source({'itemLotId': 123, 'lotSource': table}, {}, {})
                with self.assertRaises(ValueError):
                    linkage_lot_type({'itemLotId': 123, 'lotSource': table})


# Execute the real cross-reference transformation without loading SoulsFormats or
# opening a game install. This catches lost metadata between extraction stages,
# which helper-only tests cannot see.
import ast
from pathlib import Path


class ExtractionWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tree = ast.parse(Path(__file__).with_name('extract_all_items.py').read_text())
        cls.main = next(n for n in cls.tree.body if isinstance(n, ast.FunctionDef) and n.name == 'main')

    def transform(self, record, map_lots, enemy_lots):
        helpers = [n for n in self.main.body if isinstance(n, ast.FunctionDef)
                   and n.name in ('extract_lot_items', 'lot_is_guaranteed')]
        loop = next(n for n in self.main.body if isinstance(n, ast.For)
                    and isinstance(n.target, ast.Name) and n.target.id == 'tr'
                    and isinstance(n.body[0], ast.Assign)
                    and isinstance(n.body[0].targets[0], ast.Name)
                    and n.body[0].targets[0].id == 'lot_id')
        namespace = dict(
            resolve_lot_source=resolve_lot_source, treasures=[record],
            item_lots=map_lots, item_lots_enemy=enemy_lots,
            treasure_base_lots={record['itemLotId']}, database=[],
            no_lot=0, no_items=0, name_dbs={},
            categorize_item=lambda *args: ('consumable', 'consumable'),
            goods_db={}, weapon_db={}, get_grid=lambda *args: (0, 0),
            get_disp_mask=lambda *args: 'dispMask00',
        )
        module = ast.Module(body=helpers + [loop], type_ignores=[])
        exec(compile(ast.fix_missing_locations(module), '<real extraction cross-reference>', 'exec'), namespace)
        return namespace['database']

    def test_actual_base_and_sublot_keep_map_identity_for_enemy_placement(self):
        record = dict(itemLotId=100, lotSource='map', source='enemy',
                      map='m10_00_00_00', areaNo=10, p1=0, p2=0,
                      x=1, y=2, z=3, partName='enemy')
        def lot(item, flag):
            return dict(lotItemId01=item, lotItemCategory01=1,
                        lotItemNum01=1, getItemFlagId=flag)
        rows = self.transform(record, {100: lot(10, 900), 101: lot(11, 901)},
                              {100: lot(99, 999), 101: lot(98, 998)})
        self.assertEqual({r['itemLotId']: (r['lotSource'], r['items'][0]['id'])
                          for r in rows}, {100: ('map', 10), 101: ('map', 11)})

    def test_all_actual_emevd_creation_paths_explicitly_bind_map_table(self):
        creation_nodes = []
        for node in ast.walk(self.main):
            if not isinstance(node, ast.Dict):
                continue
            fields = {k.value: v for k, v in zip(node.keys, node.values)
                      if isinstance(k, ast.Constant)}
            if isinstance(fields.get('source'), ast.Constant) and fields['source'].value == 'emevd':
                creation_nodes.append(node)
        self.assertEqual(len(creation_nodes), 3)
        pos = dict(map='m10_00_00_00', areaNo=10, p1=0, p2=0,
                   x=1, y=2, z=3, name='enemy', model='c0000', npcParam=1)
        for node in creation_nodes:
            rec = eval(compile(ast.Expression(node), '<real EMEVD creation>', 'eval'),
                       dict(pos=pos, lot_id=100, lot=100, flag_id=900, event_id=1200))
            self.assertEqual(rec['lotSource'], 'map')
            rows = self.transform(rec, {100: dict(lotItemId01=10, lotItemCategory01=1)},
                                  {100: dict(lotItemId01=99, lotItemCategory01=1)})
            self.assertEqual(rows[0]['items'][0]['id'], 10)


if __name__ == '__main__':
    unittest.main()
