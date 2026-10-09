#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit, hash-locked acquisition. Nothing is downloaded or executed on import."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, build_opener, HTTPRedirectHandler

ROOT = Path(__file__).resolve().parent
ALLOWED = {'raw.githubusercontent.com', 'media.githubusercontent.com'}
MAX_BYTES = 128 * 1024 * 1024

def git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(b'blob '+str(len(data)).encode('ascii')+b'\0'+data).hexdigest()

def safe_destination(root: Path, relative: str) -> Path:
    if not isinstance(relative,str) or not relative or '\\' in relative or ':' in relative:
        raise ValueError('invalid relative destination')
    p = PurePosixPath(relative)
    if p.is_absolute() or '..' in p.parts:
        raise ValueError('destination traversal')
    target = root.resolve().joinpath(*p.parts).resolve()
    if not target.is_relative_to(root.resolve()) or target == root.resolve():
        raise ValueError('destination outside root')
    return target

def validate_spec(spec: dict) -> None:
    p = urlparse(spec.get('url',''))
    if p.scheme != 'https' or p.hostname not in ALLOWED or p.username or p.password or p.port not in (None,443) or p.query or p.fragment:
        raise ValueError('unapproved acquisition URL')
    # Source URL must identify the official repository at a full immutable revision.
    if not re.search(r'/opencv/opencv_zoo/[0-9a-f]{40}/', p.path):
        raise ValueError('URL is not pinned to the approved upstream')
    safe_destination(ROOT, spec.get('destination',''))
    if type(spec.get('bytes')) is not int or not 0 < spec['bytes'] <= MAX_BYTES:
        raise ValueError('unbounded download')
    fields = [('sha256',64), ('git_blob_sha1',40)]
    present = False
    for field, length in fields:
        if field in spec:
            present = True
            if not re.fullmatch('[0-9a-f]{'+str(length)+'}',spec[field]):
                raise ValueError('invalid hash')
    if not present:
        raise ValueError('no pinned content hash')

def verify(data: bytes, spec: dict) -> None:
    if len(data) != spec['bytes']:
        raise ValueError('size mismatch')
    if spec['destination'].endswith('.onnx') and data.startswith(b'version https://git-lfs.github.com'):
        raise ValueError('received a Git LFS pointer, not model weights')
    if 'sha256' in spec and hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError('SHA-256 mismatch')
    if 'git_blob_sha1' in spec and git_blob_sha1(data) != spec['git_blob_sha1']:
        raise ValueError('Git blob hash mismatch')

class RestrictedRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        p=urlparse(newurl)
        if p.scheme!='https' or p.hostname not in ALLOWED:
            raise ValueError('redirect left approved download hosts')
        return super().redirect_request(req,fp,code,msg,headers,newurl)

def acquire(spec: dict, root: Path = ROOT) -> dict:
    validate_spec(spec)
    dest = safe_destination(root, spec['destination'])
    if dest.exists():
        data=dest.read_bytes(); verify(data,spec)
        return {'destination':spec['destination'],'status':'already_verified','sha256':hashlib.sha256(data).hexdigest()}
    req=Request(spec['url'],headers={'User-Agent':'NeuRotic-Character-Inspector-Acquisition/1'})
    with build_opener(RestrictedRedirect()).open(req,timeout=60) as response:
        data=response.read(spec['bytes']+1)
    verify(data,spec)
    dest.parent.mkdir(parents=True,exist_ok=True)
    tmp=None
    try:
        with tempfile.NamedTemporaryFile(dir=dest.parent,delete=False) as f:
            tmp=Path(f.name);f.write(data);f.flush();os.fsync(f.fileno())
        # Exclusive publication: never replace an existing different source or user's model.
        os.link(tmp, dest) # Atomic create-if-absent on NTFS/POSIX; fails safely when unsupported.
    finally:
        if tmp: tmp.unlink(missing_ok=True)
    return {'destination':spec['destination'],'status':'downloaded_verified','sha256':hashlib.sha256(data).hexdigest()}

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sources',action='store_true')
    p.add_argument('--models',action='store_true',help='explicitly download the two Apache-licensed ONNX models')
    args=p.parse_args()
    if not (args.sources or args.models):
        p.error('choose --sources and/or --models; review LICENSE_AUDIT.md first')
    receipts=[]
    try:
        for enabled,name,key in [(args.sources,'upstream.lock.json','sources'),(args.models,'models.lock.json','models')]:
            if enabled:
                for spec in json.loads((ROOT/name).read_text(encoding='utf-8'))[key]:
                    receipt=acquire(spec);receipts.append(receipt);print(receipt['status'],receipt['destination'])
    except (OSError, ValueError) as exc:
        print('Acquisition failed:',str(exc));return 1
    (ROOT/'evidence/acquisition_receipt.json').write_text(json.dumps(receipts,indent=2)+'\n',encoding='utf-8')
    return 0
if __name__=='__main__':
    raise SystemExit(main())
