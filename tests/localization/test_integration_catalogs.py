"""Data integration regressions; native validation and rendered UI stay separate."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/localization'))
import inventory
import bundles

def entry(id='ingame.fixture.greeting', english='Hello %s'):
    return dict(id=id,surface='desktop' if id.startswith('desktop.') else 'in_game',section='fixture',control='label',english=english,context='Fixture',placeholders=inventory.placeholders(english),preserve=[],line_breaks=False,revision=2)

class BundleGenerationTests(unittest.TestCase):
    def fixture(self, root, translation='Hallo %s'):
        for path in ('localization/bundled','localization/machine-drafts','OptiScaler/menu/locales','OptiScaler/menu/localization','apps/NeuRoticHub/shipping-localization/OptiScaler/menu/localization','docs/implementation-evidence/community-languages'):
            (root/path).mkdir(parents=True,exist_ok=True)
        (root/'localization/english.json').write_text(json.dumps([entry()]),encoding='utf-8')
        (root/'localization/bundled-authored.tsv').write_text('',encoding='utf-8')
        for locale in ('es','pt','de','fr'):
            (root/f'OptiScaler/menu/locales/{locale}.json').write_text('{}',encoding='utf-8')
        pack=dict(formatVersion=1,packId='included-de',locale='de',language='Deutsch',name='Deutsch',author='Original author',version='2-draft',desktop=[],in_game=[dict(id='ingame.fixture.greeting',translation=translation,revision=0,note='Pending review')])
        (root/'localization/bundled/de.nrlang').write_text(json.dumps(pack,ensure_ascii=False),encoding='utf-8')
        return root/'localization/bundled/de.nrlang'

    def build(self, root):
        with patch.object(bundles,'ROOT',root),patch.object(bundles,'LOCALES',['de']),patch.object(bundles,'NAMES',['Deutsch']),contextlib.redirect_stdout(io.StringIO()):
            bundles.build()

    def test_repeated_generation_preserves_exact_source_translation_and_stale_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=self.fixture(root);before=source.read_bytes()
            self.build(root)
            self.assertEqual(before,source.read_bytes(),'Emission must not reassemble or overwrite curated source packs')

    def test_generated_representation_matches_both_v6_consumers_and_is_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);self.build(root)
            paths=[root/'OptiScaler/menu/localization/BundledPacks.inc',root/'apps/NeuRoticHub/shipping-localization/OptiScaler/menu/localization/BundledPacks.inc']
            self.assertTrue(all(path.exists() for path in paths),'Both selected consumers need regenerated data')
            first=[path.read_bytes() for path in paths]
            for text in (raw.decode('utf-8') for raw in first):
                self.assertIn('static const EmbeddedPack EmbeddedPacks[]',text)
                self.assertIn('"included-de","de",EmbeddedPack0Chunks',text)
                data=json.loads(''.join(re.findall(r'R"NR_BUNDLE\((.*?)\)NR_BUNDLE"',text,re.S)))
                self.assertEqual('Hallo %s',data['in_game'][0]['translation'])
                self.assertEqual(0,data['in_game'][0]['revision'])
            self.build(root);self.assertEqual(first,[path.read_bytes() for path in paths])

    def test_invalid_translation_format_is_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root,'Hallo %n')
            with self.assertRaises(ValueError):self.build(root)

class IntegratedCatalogTests(unittest.TestCase):
    def test_generated_inventories_match_explicit_consumer_scope(self):
        canonical=inventory.load()
        for consumer,path in inventory.CONSUMER_OUTPUTS.items():
            text=(ROOT/path/'EnglishInventory.inc').read_text(encoding='utf-8')
            generated=json.loads(''.join(re.findall(r'R"NR_ENGLISH\((.*?)\)NR_ENGLISH"',text,re.S)))
            self.assertEqual(inventory.consumer_entries(canonical,consumer),generated)
            self.assertFalse(any(marker in text for marker in ('<<<<<<<','>>>>>>>')))

    def test_generated_packs_preserve_exact_source_rows_per_consumer(self):
        canonical=inventory.load()
        for consumer,path in inventory.CONSUMER_OUTPUTS.items():
            known={e['id'] for e in inventory.consumer_entries(canonical,consumer)}
            text=(ROOT/path/'BundledPacks.inc').read_text(encoding='utf-8')
            arrays={}
            for name,body in re.findall(r'static const char\* const (\w+)\[\]\s*=\s*\{(.*?)\};',text,re.S):
                arrays[name]=json.loads(''.join(re.findall(r'R"NR_BUNDLE\((.*?)\)NR_BUNDLE"',body,re.S)))
            table=re.findall(r'\{"([^"]+)","([^"]+)",(\w+),std::size\(\w+\)\}',text)
            self.assertEqual(len(bundles.LOCALES),len(table))
            for pack_id,locale,name in table:
                source=json.loads((ROOT/f'localization/bundled/{locale}.nrlang').read_text(encoding='utf-8'))
                expected=dict(source,**{s:[row for row in source[s] if row['id'] in known] for s in ('desktop','in_game')})
                self.assertEqual(expected,arrays[name],consumer+'/'+locale)
                self.assertEqual(pack_id,arrays[name]['packId'])

    def test_released_and_new_bound_ids_exist_in_persistent_source(self):
        ids={row['id'] for row in inventory.load()}
        for id in ('desktop.closing.title','ingame.menu-layout.heading','ingame.timing.game_fps','ingame.input-sources.strict'):
            self.assertTrue(id in ids,id)

    def test_included_translation_source_retains_release_rows(self):
        for locale in bundles.LOCALES:
            pack=json.loads((ROOT/f'localization/bundled/{locale}.nrlang').read_text(encoding='utf-8'))
            ids={row['id'] for surface in ('desktop','in_game') for row in pack[surface]}
            self.assertTrue('desktop.closing.title' in ids,locale+' missing closing')
            self.assertTrue('ingame.menu-layout.heading' in ids,locale+' missing layout')

    def test_stage_layout_participates_in_binding_audit(self):
        self.assertTrue(ROOT/'OptiScaler/menu/NrAdvancedStageLayout.inl' in inventory.paths(),'Stage layout is outside inventory discovery')

    def test_mode_selector_source_keeps_saved_unavailable_and_two_in_game_choices(self):
        source=(ROOT/'OptiScaler/dlssnr/DlssNr_StageControls.h').read_text(encoding='utf-8')
        self.assertTrue('constexpr int methodChoices[] = {0, 1}' in source,'In-game selector exposes desktop-only method')
        self.assertTrue('ComboWithSavedPreview' in source,'Missing saved unavailable preview')
        self.assertTrue('SelectUnifiedMethod(config, methodChoices[method])' in source,'Missing explicit method mapping')
        self.assertNotRegex(source,r'ImGui::Combo\("##NrMethod",\s*&method,\s*UnifiedMethods,\s*3\)')

if __name__=='__main__':unittest.main()
