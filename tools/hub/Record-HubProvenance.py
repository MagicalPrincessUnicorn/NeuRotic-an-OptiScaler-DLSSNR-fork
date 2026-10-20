"""Record local Hub inputs; never imports a donor build graph or runs donor code."""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'apps/NeuRoticHub/provenance'
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path, value): path.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')

def main():
    OUT.mkdir(exist_ok=True)
    donors=[]
    cache=Path('C:/NeuRotic/_deps/hub/donors')
    for folder in sorted(cache.iterdir()):
        identity=json.loads((folder/'SOURCE.json').read_text(encoding='utf-8-sig'))
        source=next(p for p in folder.iterdir() if p.is_dir())
        selected=[source/'LICENSE']
        if identity['id']=='swapper':
            selected += list(source.rglob('SteamLibrary.cs'))
            selected += list(source.glob('src/DLSS Swapper.csproj'))
        donors.append({**identity,'intake':'pinned archive, read-only research',
            'importedFiles':[], 'selectedResearch':[{'path':str(p.relative_to(source)), 'sha256':sha(p)} for p in selected],
            'adaptation': 'Independent manifest discovery and KeyValues parser; no mutation, downloader, driver control, WinUI, artwork, ValveKeyValue or donor NuGet graph' if identity['id']=='swapper' else 'Research only; capture/runtime code queued',
            'licenseDisposition':'Exact GPLv3 license text retained at donor cache. Version qualifier and file-level notices require public release review; no public publication in this task.'})
    write(OUT/'donor-import-map.json',{'schemaVersion':1,'donors':donors})
    files=[ROOT/'LICENSE',ROOT/'OptiScaler/include/imgui/LICENSE.txt',ROOT/'OptiScaler/menu/font/Hack-Regular.ttf', ROOT/'OptiScaler/menu/font/Hack_Compressed.h', ROOT/'external/nlohmann/json.hpp']
    files+=list((ROOT/'apps/NeuRoticHub/assets').iterdir())
    write(OUT/'local-inputs.json',{'schemaVersion':1,'files':[{'path':str(p.relative_to(ROOT)),'sha256':sha(p)} for p in files],
        'theme':'Current menu_common.cpp ApplyThemeStyle leaf; Moonlight / Madam Herta attribution retained; no injected state dependency',
        'brandArchiveSha256':'249e10649a37184f5ed9fdbf743e71e3d7b6f92dbb79da74af52040b6cd55422',
        'brandReference':'nr-experimental/docs/brand/BRAND_ASSETS.md',
        'discoveryDependencies':'Self-contained .NET 10.0 win-x64; no explicit third-party NuGet packages',
        'nativeDependencies':'Static MSVC CRT, Windows SDK D3D11/WIC and current ImGui/STB/nlohmann source; no capture, NGX or SDK link graph'})
    font=(ROOT/'OptiScaler/menu/font/Hack-Regular.ttf').read_bytes()
    tables=struct.unpack_from('>H',font,4)[0]
    offset=next(struct.unpack_from('>4sIII',font,12+i*16)[2] for i in range(tables) if font[12+i*16:16+i*16]==b'name')
    _,count,strings=struct.unpack_from('>HHH',font,offset)
    notices=[]
    for i in range(count):
        platform,encoding,language,name,length,start=struct.unpack_from('>6H',font,offset+6+i*12)
        if name not in (0,13,14): continue
        value=font[offset+strings+start:offset+strings+start+length].decode('utf-16-be' if platform in (0,3) else 'mac_roman')
        if value not in notices: notices.append(value)
    (OUT/'Hack-font-notices.txt').write_text('\n\n'.join(notices)+'\n',encoding='utf-8')
    print('Recorded pinned donor research, brand, native inputs and embedded font notices')
if __name__=='__main__': main()
