"""Read explicitly selected offline dependencies from a hash-bound external inventory.

Planning inspects only ZIP metadata. Reading never selects first-party binaries.
The caller must explicitly select models; this module never executes any input.
"""
import hashlib
import json
from pathlib import Path
import re
import stat
import zipfile
import os

def owned_output(root, path, *, tree=False):
    root=Path(os.path.abspath(root));path=Path(os.path.abspath(path))
    require(path.is_relative_to(root/'builds'), 'Output must stay inside this checkout builds')
    for parent in [path,*path.parents]:
        require(not parent.is_symlink() and not parent.is_junction(), 'Linked output refused: '+str(parent))
    if tree and path.is_dir():
        pending=[path]
        while pending:
            for child in pending.pop().iterdir():
                require(not child.is_symlink() and not child.is_junction(), 'Linked output descendant: '+str(child))
                if child.is_dir():pending.append(child)
    return path

def write_owned(root,path,raw):
    # Exclusive build ownership is required. Atomic replacement never opens a
    # linked final file; ancestry is checked again at each direct mutation.
    path=owned_output(root,path);temp=owned_output(root,path.with_name(path.name+'.nr-write.tmp'))
    require(not temp.exists(),'Existing write temporary: '+str(temp))
    owned_output(root,path.parent);path.parent.mkdir(parents=True,exist_ok=True)
    try:
        with temp.open('xb') as stream:stream.write(raw)
        owned_output(root,path);owned_output(root,temp);os.replace(temp,path)
    finally:
        if temp.exists():owned_output(root,temp);temp.unlink()

ACCEPTED_ARCHIVE_SHA256='9d20d8dad9d5cd2da102190cc625b9646ed51f2146f7275edf965c0478144ced'
ACCEPTED_INVENTORY_SHA256='8b8fd77905e63d555a5ec38f1888125546b778cb6e5bb85aa481fc77e2168df2'

def require(condition, reason):
    if not condition: raise ValueError(reason)

def file_sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()

def safe_name(name):
    require(isinstance(name,str) and name and '\\' not in name and ':' not in name and '\x00' not in name, 'Unsafe member path')
    for part in name.split('/'):
        require(part and part not in ('.','..') and part==part.rstrip(' .'), 'Unsafe member path: '+name)
        require(not re.fullmatch(r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?',part), 'Windows device path')
    return name

def dependency_kind(name):
    """A narrow path policy, applied in addition to the exact approved inventory."""
    if name.endswith(('.pyc','.pdb','.obj','.lib')):return None
    if name.startswith(('licenses/','packages/current/payload/Licenses/')):
        return 'notice' if Path(name).suffix.lower() in ('.txt','.md','.json') else None
    if name=='packages/current/payload/NeuRotic-LICENSE.txt':return 'notice'
    if name.startswith('discovery/') and name.count('/')==1:
        base=name.split('/')[-1]
        if base.startswith(('System.','Microsoft.')) and base.endswith('.dll'):return 'dotnet-runtime'
        if base in ('LICENSE.TXT','THIRD-PARTY-NOTICES.TXT','coreclr.dll','clrjit.dll','clrgc.dll','clretwrc.dll','hostfxr.dll','hostpolicy.dll','mscordaccore.dll','mscordbi.dll','mscorlib.dll','netstandard.dll','createdump.exe','msquic.dll','vcruntime140_cor3.dll'):return 'dotnet-runtime'
    match=re.fullmatch(r'packages/current/payload/OptiScaler/CharacterInspector/Version [1-9][0-9]*/(runtime|packages|models)/(.+)',name)
    if match:return 'inspector-model' if match[1]=='models' else 'inspector-dependency'
    if name=='packages/current/payload/OptiScaler/amd_fidelityfx_dx12.dll':return 'fidelityfx-runtime'
    return None

class DependencyArchive:
    def __init__(self, archive, archive_sha256, inventory, inventory_sha256):
        self.archive=Path(archive);self.inventory=Path(inventory)
        self.archive_sha256=archive_sha256;self.inventory_sha256=inventory_sha256
        self.stable()
        document=json.loads(self.inventory.read_bytes()); self.document=document
        self.rows={};folded=set()
        for row in document['inventory']['files']:
            name=safe_name(row['path'].replace('\\','/'));key=name.casefold()
            require(key not in folded,'Duplicate inventory path: '+name);folded.add(key)
            require(type(row['bytes']) is int and row['bytes']>=0 and re.fullmatch('[0-9a-f]{64}',row['sha256']), 'Invalid inventory row')
            self.rows[name]={'path':name,'bytes':row['bytes'],'sha256':row['sha256']}
        with zipfile.ZipFile(self.archive) as z:
            names=set();folded=set()
            for item in z.infolist():
                full=safe_name(item.filename)
                require(full.startswith('NeuRotic/'),'Unexpected archive root')
                require(full.casefold() not in folded,'Duplicate ZIP member: '+full);folded.add(full.casefold())
                require(not item.is_dir() and not stat.S_ISLNK(item.external_attr>>16) and not item.flag_bits&1,'Non-regular/encrypted ZIP member')
                name=full.removeprefix('NeuRotic/');names.add(name)
                require(name in self.rows and item.file_size==self.rows[name]['bytes'],'ZIP inventory/size mismatch: '+name)
            require(names==set(self.rows),'ZIP does not match exact external inventory')
    def stable(self):
        require(file_sha(self.archive)==self.archive_sha256,'Dependency archive hash differs')
        require(file_sha(self.inventory)==self.inventory_sha256,'External inventory hash differs')
    def eligible(self, *, allow_models=False):
        return sorted(n for n in self.rows if dependency_kind(n) and (allow_models or dependency_kind(n)!='inspector-model'))
    def plan(self, names, *, allow_models=False):
        names=list(names);require(len(names)==len({n.casefold() for n in names}),'Duplicate dependency selection')
        selected=[]
        for name in names:
            safe_name(name);require(name in self.rows,'Selected dependency absent: '+name)
            kind=dependency_kind(name);require(kind is not None,'Not an inherited dependency: '+name)
            require(allow_models or kind!='inspector-model','Model selection requires explicit opt-in')
            selected.append({**self.rows[name],'kind':kind})
        return selected
    def read(self, names, *, allow_models=False):
        rows=self.plan(names,allow_models=allow_models);self.stable();files={}
        with zipfile.ZipFile(self.archive) as z:
            for row in rows:
                raw=z.read('NeuRotic/'+row['path'])
                require(len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256'],'Dependency content mismatch: '+row['path'])
                files[row['path']]=raw
        self.stable();return files
