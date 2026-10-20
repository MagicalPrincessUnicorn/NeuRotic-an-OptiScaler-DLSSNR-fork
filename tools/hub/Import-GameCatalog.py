"""Derive a compact, read-only recommendation index from the supplied research catalog."""
import hashlib, json
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = root / 'references/hub-game-catalog/20261002/NeuRotic_Game_Catalog.json'
catalog = json.loads(source.read_text(encoding='utf-8-sig'))
approved = {'dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll'}
games = []
for game in catalog['games']:
    profiles = [{'api': p['api'], 'proxy': p['suggested_proxy_filename']} for p in game['graphics_profiles'] if p.get('suggested_proxy_filename') in approved]
    games.append({'name':game['name'], 'aliases':game.get('aliases',[]), 'ids':game['identity'].get('store_ids',[]), 'profiles':profiles, 'safety':game.get('safety',{}).get('status','unknown')})
for game in games:
    game['ids']=[{'store':p['store'],'id':str(p['id'])} for p in game['ids']]
data = {'version':catalog['catalog_version'],'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'games':games}
target = root / 'apps/NeuRoticHub/library/GameCatalogData.h'
payload=json.dumps(data,ensure_ascii=True,separators=(',',':'))
chunks='\n'.join('R\"NRCATALOG('+payload[i:i+8000]+')NRCATALOG\"' for i in range(0,len(payload),8000))
target.write_text('// Derived research hints; see Import-GameCatalog.py.\n#pragma once\nnamespace nh { inline constexpr char GameCatalogData[]=\n'+chunks+'; }\n',encoding='utf-8')
(root / 'tools/packaging/customer/support/NeuRotic-GameCatalog.json').write_text(payload+'\n',encoding='utf-8')
print(f'Imported {len(games)} research entries; source SHA256 {data["sourceSha256"]}')
