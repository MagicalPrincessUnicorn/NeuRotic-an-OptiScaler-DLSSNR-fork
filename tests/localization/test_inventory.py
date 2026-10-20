import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('inventory', ROOT / 'tools/localization/inventory.py')
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)

class InventoryTests(unittest.TestCase):
    def entry(self):
        return dict(id='desktop.install.foreign_file',surface='desktop',section='installation',control='warning',english='{filename} appears to not have come from NeuRotic. How should we proceed?',context='Foreign file decision',placeholders=[dict(name='filename',type='text')],preserve=['NeuRotic'],line_breaks=False,revision=1)

    def test_complete_entry(self):
        inventory.validate([self.entry()])

    def test_duplicate_ids(self):
        with self.assertRaises(ValueError):inventory.validate([self.entry(),self.entry()])

    def test_missing_english(self):
        entry=self.entry();entry['english']=''
        with self.assertRaises(ValueError):inventory.validate([entry])

    def test_placeholder_metadata(self):
        entry=self.entry();entry['placeholders']=[]
        with self.assertRaises(ValueError):inventory.validate([entry])

    def test_surface(self):
        entry=self.entry();entry['surface']='cmd'
        with self.assertRaises(ValueError):inventory.validate([entry])

    def test_canonical_current(self):
        entries=inventory.load();inventory.validate(entries)
        self.assertEqual({'desktop','in_game'},{e['surface'] for e in entries})
        self.assertTrue(any('How should we proceed?' in e['english'] for e in entries))
        self.assertTrue(any('presentation progress observed' in e['english'] for e in entries))

    def test_production_control_preview_metadata(self):
        entries={e['id']:e for e in inventory.load()}
        self.assertEqual('button',entries['desktop.hubshell.install_d4824a37']['control'])
        self.assertEqual('checkbox',entries['desktop.hubshell.reduced_motion_ee127977']['control'])
        self.assertTrue({'button','checkbox','header','tooltip','warning','status'} <= {e['control'] for e in entries.values()})

if __name__=='__main__':unittest.main()
