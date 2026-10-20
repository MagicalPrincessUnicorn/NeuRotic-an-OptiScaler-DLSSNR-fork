"""Verify donor attribution survives packaging, alongside the exact payload seal."""
from pathlib import Path
import sys, zipfile, json, hashlib
root=Path(__file__).resolve().parents[1]
archive=Path(sys.argv[1])
with zipfile.ZipFile(archive) as z:
    prefix=archive.stem+'/'
    license_text=z.read(prefix+'payload/NeuRotic-LICENSE.txt').decode('utf-8-sig').replace('\r\n','\n')
    required=(root/'OptiScaler/mfg/THIRD_PARTY_MFGAdaUnlock_LICENSE').read_text(encoding='utf-8-sig').replace('\r\n','\n').strip()
    assert required in license_text, 'The installed product license must carry the copied MFG donor MIT notice'
    collector=(root/'tools/packaging/customer/support/Collect-NR-Review.ps1').read_bytes().replace(
        b"'reviews'",b"'diagnostics'").replace(b'REVIEW_ZIP=',b'DIAGNOSTICS_ZIP=')
    assert z.read(prefix+'support/Collect-Diagnostics.ps1')==collector, 'Launcher must use current verified cleanup collector'
    assert b'Collect-Diagnostics.ps1' in z.read(prefix+'Collect-Diagnostics.cmd')
    manifest=json.loads(z.read(prefix+'support/PACKAGE-MANIFEST.json'))
    for row in manifest['files']:
        raw=z.read(prefix+row['path'].replace('\\','/'))
        assert len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256'].lower(),row['path']
print('PASS donor MIT attribution and all exact package manifest payload identities')
