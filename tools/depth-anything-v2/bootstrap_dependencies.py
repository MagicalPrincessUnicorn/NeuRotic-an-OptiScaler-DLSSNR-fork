"""Acquire the approved official source archive and checkpoint; never create a Git checkout."""
import argparse, hashlib, io, json, os, tarfile, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda:stream.read(1024*1024),b''): h.update(data)
    return h.hexdigest()
def acquire(url,path,limit,sha=None):
    path=Path(path)
    if path.exists():
        if sha and digest(path)!=sha: raise RuntimeError('Existing dependency hash mismatch')
        return
    partial=path.with_suffix(path.suffix+'.part');path.parent.mkdir(parents=True,exist_ok=True)
    try:
        with urllib.request.urlopen(url,timeout=60) as response,partial.open('wb') as out:
            size=0
            while data:=response.read(1024*1024):
                size+=len(data)
                if size>limit:raise RuntimeError('Dependency download exceeds bound')
                out.write(data)
        if sha and digest(partial)!=sha:raise RuntimeError('Dependency hash mismatch')
        os.replace(partial,path)
    finally:
        if partial.exists():partial.unlink()
def verify_source(source,commit):
    receipt=json.loads((source/'.source-receipt.json').read_text())
    if receipt['commit']!=commit:raise RuntimeError('Source revision mismatch')
    for name,sha in receipt['files'].items():
        if digest(source/name)!=sha:raise RuntimeError('Modified pinned source: '+name)
    actual={p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file() and p.name!='.source-receipt.json'}
    if actual!=set(receipt['files']):raise RuntimeError('Source archive has added/removed files')
    return commit
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--destination',type=Path,default=ROOT.parents[1]/'_deps/depth-anything-v2');a=ap.parse_args()
    lock=json.loads((ROOT/'dependencies.lock.json').read_text());commit=lock['official_source']['commit'];archive=a.destination/'official-source.tar.gz'
    acquire('https://codeload.github.com/DepthAnything/Depth-Anything-V2/tar.gz/'+commit,archive,100<<20)
    source=a.destination/'Depth-Anything-V2'
    if not source.exists():
        source.mkdir()
        with tarfile.open(archive) as tar:
            for member in tar:
                parts=Path(member.name).parts
                if len(parts)<2:continue
                member.name=Path(*parts[1:]).as_posix()
                if member.isdir() or member.isfile():tar.extract(member,source,filter='data')
                else:raise RuntimeError('Source archive contains non-file member')
        files={p.relative_to(source).as_posix():digest(p) for p in source.rglob('*') if p.is_file()}
        (source/'.source-receipt.json').write_text(json.dumps({'commit':commit,'archiveSha256':digest(archive),'files':files},indent=2))
    verify_source(source,commit)
    checkpoint=lock['checkpoint'];acquire(checkpoint['url'],a.destination/'checkpoints'/checkpoint['filename'],checkpoint['max_download_bytes'],checkpoint['sha256'])
    print('Pinned official source archive and verified Small checkpoint ready:',a.destination)
if __name__=='__main__':main()
