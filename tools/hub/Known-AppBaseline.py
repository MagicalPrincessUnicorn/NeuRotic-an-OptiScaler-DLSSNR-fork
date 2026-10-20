"""Reconstruct the exact previously delivered App state without copying build trees."""
import hashlib, json, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT.parent/'_exports/NeuRotic-Current-Flagship-4134edeb.zip'
PREVIOUS=ROOT.parent/'_exports/NeuRotic-App-31895b6e-Update.zip'
BASE_SHA='2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
PREVIOUS_SHA='dd958a5aa8ac18db967560f2c88c4719890bfa7595df1bcda775f3a1e4e628b7'
NAVIGATION=ROOT.parent/'_exports/NeuRotic-App-63723cdc-Update.zip'
NAVIGATION_SHA='51ae8b047d59f26a18f2a75957d5382f67e0b47c37b660aa9fff6131475c72f9'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def archive(path,expected):
 for p in (path,*path.parents):
  if p.is_symlink() or p.is_junction():raise RuntimeError('Linked baseline archive refused')
 if sha(path.read_bytes())!=expected:raise RuntimeError('Unsupported baseline archive')
 with zipfile.ZipFile(path) as z:
  names=z.namelist()
  if len({n.casefold() for n in names})!=len(names) or z.testzip():raise RuntimeError('Invalid baseline ZIP')
  for n in names:
   if n.startswith('/') or ':' in n or '\\' in n or '..' in n.split('/'):raise RuntimeError('Unsafe baseline member')
  return {n:z.read(n) for n in names}
def current_app_files():
 files={n.removeprefix('NeuRotic/'):raw for n,raw in archive(BASE,BASE_SHA).items()}
 sealed=json.loads(files['ARCHIVE-MANIFEST.json'])
 for row in sealed['files']:
  if sha(files[row['path']])!=row['sha256'] or len(files[row['path']])!=row['bytes']:raise RuntimeError('Baseline seal differs')
 patch=archive(PREVIOUS,PREVIOUS_SHA);manifest=json.loads(patch['APP-UPDATE-MANIFEST.json'])
 if manifest['baseArchiveSha256']!=BASE_SHA or manifest['kind']!='neurotic-app-delta':raise RuntimeError('Previous update identity differs')
 for row in manifest['files']:
  name=row['path'];old=files.get(name)
  if (sha(old) if old is not None else None)!=row['previousSha256']:raise RuntimeError('Previous update preimage differs: '+name)
  if row['action']=='put':
   raw=patch['NeuRotic/'+name]
   if sha(raw)!=row['sha256'] or len(raw)!=row['bytes']:raise RuntimeError('Previous update payload differs')
   files[name]=raw
  elif row['action']=='remove':files.pop(name,None)
  else:raise RuntimeError('Unsupported previous update operation')
 for row in manifest['dependencies']:
  if sha(files[row['path']])!=row['sha256']:raise RuntimeError('Previous dependency differs')
 sealed=json.loads(files['ARCHIVE-MANIFEST.json'])
 if {r['path'] for r in sealed['files']}!=set(files)-{'ARCHIVE-MANIFEST.json'}:raise RuntimeError('Updated seal inventory differs')
 for row in sealed['files']:
  if sha(files[row['path']])!=row['sha256']:raise RuntimeError('Updated seal hash differs')
 return files,manifest

def navigation_app_files():
 files,_=current_app_files()
 patch=archive(NAVIGATION,NAVIGATION_SHA);manifest=json.loads(patch['APP-UPDATE-MANIFEST.json'])
 if manifest['baseArchiveSha256']!=PREVIOUS_SHA or manifest['kind']!='neurotic-app-delta':raise RuntimeError('Navigation update identity differs')
 for row in manifest['files']:
  name=row['path'];old=files.get(name)
  if (sha(old) if old is not None else None)!=row['previousSha256']:raise RuntimeError('Navigation preimage differs: '+name)
  if row['action']=='put':
   raw=patch['NeuRotic/'+name]
   if sha(raw)!=row['sha256'] or len(raw)!=row['bytes']:raise RuntimeError('Navigation payload differs')
   files[name]=raw
  elif row['action']=='remove':files.pop(name,None)
  else:raise RuntimeError('Unsupported navigation operation')
 for row in manifest['dependencies']:
  if sha(files[row['path']])!=row['sha256']:raise RuntimeError('Navigation dependency differs')
 sealed=json.loads(files['ARCHIVE-MANIFEST.json'])
 if {r['path'] for r in sealed['files']}!=set(files)-{'ARCHIVE-MANIFEST.json'}:raise RuntimeError('Navigation seal inventory differs')
 for row in sealed['files']:
  if sha(files[row['path']])!=row['sha256'] or len(files[row['path']])!=row['bytes']:raise RuntimeError('Navigation seal hash differs')
 return files,manifest

COMPACT=ROOT.parent/'_exports/NeuRotic-App-4992bece-Update.zip'
COMPACT_SHA='972c555e4d9b406a4398d5d9e678308f35e0f520fd9c436b76e6abff38286d67'

def compact_app_files():
 files,_=navigation_app_files()
 patch=archive(COMPACT,COMPACT_SHA);manifest=json.loads(patch['APP-UPDATE-MANIFEST.json'])
 if manifest['baseArchiveSha256']!=NAVIGATION_SHA or manifest['kind']!='neurotic-app-delta':raise RuntimeError('Compact update identity differs')
 for row in manifest['files']:
  name=row['path'];old=files.get(name)
  if (sha(old) if old is not None else None)!=row['previousSha256']:raise RuntimeError('Compact preimage differs: '+name)
  if row['action']=='put':
   raw=patch['NeuRotic/'+name]
   if sha(raw)!=row['sha256'] or len(raw)!=row['bytes']:raise RuntimeError('Compact payload differs')
   files[name]=raw
  elif row['action']=='remove':files.pop(name,None)
  else:raise RuntimeError('Unsupported compact operation')
 for row in manifest['dependencies']:
  if sha(files[row['path']])!=row['sha256']:raise RuntimeError('Compact dependency differs')
 sealed=json.loads(files['ARCHIVE-MANIFEST.json'])
 if {r['path'] for r in sealed['files']}!=set(files)-{'ARCHIVE-MANIFEST.json'}:raise RuntimeError('Compact seal inventory differs')
 for row in sealed['files']:
  if sha(files[row['path']])!=row['sha256'] or len(files[row['path']])!=row['bytes']:raise RuntimeError('Compact seal hash differs')
 return files,manifest

READABLE=ROOT.parent/'_exports/NeuRotic-App-28f09848-Update.zip'
READABLE_SHA='cfb94f27268b6a10182a426f01e9e0ffbd3bc3e70f86d6f6ae8b9e73b2ebd12a'

def readable_app_files():
 files,_=compact_app_files()
 patch=archive(READABLE,READABLE_SHA);manifest=json.loads(patch['APP-UPDATE-MANIFEST.json'])
 if manifest['baseArchiveSha256']!=COMPACT_SHA or manifest['kind']!='neurotic-app-delta':raise RuntimeError('Readable update identity differs')
 for row in manifest['files']:
  name=row['path'];old=files.get(name)
  if (sha(old) if old is not None else None)!=row['previousSha256']:raise RuntimeError('Readable preimage differs: '+name)
  if row['action']=='put':
   raw=patch['NeuRotic/'+name]
   if sha(raw)!=row['sha256'] or len(raw)!=row['bytes']:raise RuntimeError('Readable payload differs')
   files[name]=raw
  elif row['action']=='remove':files.pop(name,None)
  else:raise RuntimeError('Unsupported readable operation')
 for row in manifest['dependencies']:
  if sha(files[row['path']])!=row['sha256']:raise RuntimeError('Readable dependency differs')
 sealed=json.loads(files['ARCHIVE-MANIFEST.json'])
 if {r['path'] for r in sealed['files']}!=set(files)-{'ARCHIVE-MANIFEST.json'}:raise RuntimeError('Readable seal inventory differs')
 for row in sealed['files']:
  if sha(files[row['path']])!=row['sha256'] or len(files[row['path']])!=row['bytes']:raise RuntimeError('Readable seal hash differs')
 return files,manifest
