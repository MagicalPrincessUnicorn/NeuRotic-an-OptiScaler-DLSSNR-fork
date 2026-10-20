"""Verify preserved research bytes without the optional jsonschema dependency."""
import hashlib,json
from pathlib import Path
packet=Path(r'C:\NeuRotic\artifacts\research\chatgpt-rnd\extracted\NeuRotic_Chat_G_Implementation_Closure_v1')
manifest=json.loads((packet/'08_MANIFEST.json').read_text(encoding='utf-8'))
entries=manifest['files']
for entry in entries:
    path=packet/entry['path'];payload=path.read_bytes()
    assert len(payload)==entry['bytes'] and hashlib.sha256(payload).hexdigest()==entry['sha256'],entry['path']
print('preserved packet manifest:',len(entries),'payload hashes passed')
for name in ('candidate_record.schema.json','candidate_examples.json'):
    assert (packet/'support'/name).read_bytes()==(Path(__file__).resolve().parents[1]/'tools/diagnostics/candidate_observer'/name).read_bytes()
print('vendored schema/examples: exact original bytes passed')
