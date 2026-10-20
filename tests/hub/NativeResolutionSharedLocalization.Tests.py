"""Shared-ID and embedded-pack contract; no native runtime or compiler."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXTS = {
    'ingame.native-resolution.pre': 'Match Native (Pre Super Resolution)',
    'ingame.native-resolution.post': 'Match Native (Post Super Resolution)',
    'ingame.native-resolution.unknown': 'Unknown Native resolution preference',
    'ingame.native-resolution.help': 'Both Match Native options use the native input dimensions. The selected stage controls when Neural Rendering runs.',
}
DESKTOP_EQUIVALENTS = {
    'ingame.native-resolution.pre': 'desktop.settings.native_resolution_pre',
    'ingame.native-resolution.post': 'desktop.settings.native_resolution_post',
    'ingame.native-resolution.help': 'desktop.settings.native_resolution_help',
}

def check(condition, message):
    assert condition, message
    print('PASS ' + message)

def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

entries = load(ROOT / 'localization/english.json')
by_id = {row['id']: row for row in entries}
for identity, text in TEXTS.items():
    check(identity in by_id and by_id[identity]['english'] == text and by_id[identity]['surface'] == 'in_game', 'exact shared English ' + identity)
packs = {}
for locale in ('de', 'es', 'fr', 'pl', 'ja', 'ko', 'pt-BR', 'ru', 'zh-CN'):
    path = ROOT / 'localization/bundled' / (locale + '.nrlang')
    pack = load(path)
    desktop = {row['id']: row for row in pack['desktop']}
    ingame = {row['id']: row for row in pack['in_game']}
    for identity in TEXTS:
        check(identity in ingame and ingame[identity]['translation'].strip() and ingame[identity]['revision'] >= 0, pack['locale'] + ' shared translation ' + identity)
    for identity, desktop_identity in DESKTOP_EQUIVALENTS.items():
        check(ingame[identity]['translation'] == desktop[desktop_identity]['translation'], pack['locale'] + ' shared/desktop translation agreement ' + identity)
    packs[pack['locale']] = pack
check(len(packs) == 9, 'all nine included packs checked')

for consumer in ('OptiScaler/menu/localization', 'apps/NeuRoticHub/shipping-localization/OptiScaler/menu/localization'):
    inventory_text = (ROOT / consumer / 'EnglishInventory.inc').read_text(encoding='utf-8')
    inventory = json.loads(''.join(re.findall(r'R"NR_ENGLISH\(([\s\S]*?)\)NR_ENGLISH"', inventory_text)))
    known = {row['id']: row for row in inventory}
    for identity, text in TEXTS.items():
        check(identity in known and known[identity]['english'] == text, consumer + ' embedded ID ' + identity)
    bundle_text = (ROOT / consumer / 'BundledPacks.inc').read_text(encoding='utf-8')
    decoded = []
    for chunk_group in re.findall(r'static const char\* const EmbeddedPack\d+Chunks\[\] = \{([\s\S]*?)\n\};', bundle_text):
        decoded.append(json.loads(''.join(re.findall(r'R"NR_BUNDLE\(([\s\S]*?)\)NR_BUNDLE"', chunk_group))))
    check(len(decoded) == 9, consumer + ' contains nine embedded packs')
    for pack in decoded:
        rows = {row['id']: row for row in pack['in_game']}
        for identity in TEXTS:
            expected = next(row for row in packs[pack['locale']]['in_game'] if row['id'] == identity)
            check(rows.get(identity) == expected, consumer + ' embedded translation ' + pack['locale'] + '/' + identity)
print('PASS Native resolution shared localization contract')
