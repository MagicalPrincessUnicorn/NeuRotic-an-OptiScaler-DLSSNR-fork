"""The bootstrap label belongs to the desktop consumer, not the game DLL."""
from pathlib import Path
import importlib.util,json,re
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('startup_inventory',root/'tools/localization/inventory.py')
inventory=importlib.util.module_from_spec(spec);spec.loader.exec_module(inventory)
entries=inventory.load();inventory.validate(entries)
label=[e for e in entries if e['id']=='desktop.startup.loading']
assert len(label)==1 and label[0]['surface']=='desktop' and label[0]['english']=='Loading...'
for consumer,relative in inventory.CONSUMER_OUTPUTS.items():
    selected=inventory.consumer_entries(entries,consumer)
    assert any(e['id']==label[0]['id'] for e in selected)==(consumer=='desktop')
    source=(root/relative/'EnglishInventory.inc').read_text(encoding='utf-8')
    chunks=re.findall(r'R"NR_ENGLISH\((.*?)\)NR_ENGLISH"',source,re.S)
    assert chunks and json.loads(''.join(chunks))==selected,consumer
print('PASS startup Loading label and exact desktop/runtime generated consumer projections')
