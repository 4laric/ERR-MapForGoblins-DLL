import unittest
from make_ap_ini import apply_preset

class PresetTests(unittest.TestCase):
    def test_preserves_other_settings_and_comments(self):
        source = '[Loot]\r\nshow_material_nodes = true ; nodes\r\nshow_crafting_materials = false\r\n[Overlay]\r\nfont_scale = 1.7\r\n'
        result = apply_preset(source)
        self.assertIn('show_material_nodes = false ; nodes\r\n', result)
        self.assertIn('show_crafting_materials = true\r\n', result)
        self.assertIn('font_scale = 1.7\r\n', result)
        self.assertEqual(result, apply_preset(result))
    def test_rejects_ambiguous_duplicates(self):
        for source in ('[Loot]\n[Loot]\n', '[Loot]\nshow_material_nodes=true\nshow_material_nodes=false\n'):
            with self.assertRaises(ValueError):
                apply_preset(source)

if __name__ == '__main__':
    unittest.main()
