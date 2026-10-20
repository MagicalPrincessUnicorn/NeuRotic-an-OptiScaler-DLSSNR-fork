"""Verify/prepare the accepted package. Never load DLLs or start providers."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import shutil
import zipfile

MANIFEST_SHA256 = 'df2159f8499ddfcb756e40cbfc7e1687a04a4776ffb4295bc420f04470739a61'

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()

def load_manifest():
    path = Path(__file__).with_name('CANONICAL_PROVIDER_MANIFEST.json')
    if sha256(path) != MANIFEST_SHA256:
        raise ValueError('CANONICAL_MANIFEST_CHANGED')
    return json.loads(path.read_bytes())

def members(manifest):
    result = {}
    names = set()
    for entry in manifest['files']:
        name = entry['path']
        path = PurePosixPath(name)
        if (not name or '\\' in name or ':' in name or path.is_absolute() or
                any(part in ('', '.', '..') for part in name.split('/')) or len(path.parts) != 2):
            raise ValueError('UNSAFE_MEMBER_PATH')
        if name in result or path.name.casefold() in names:
            raise ValueError('DUPLICATE_MEMBER')
        if path.name.casefold() in {manifest['archive']['filename'].casefold(), 'canonical-package-receipt.json'}:
            raise ValueError('RESERVED_MEMBER_NAME')
        result[name] = entry
        names.add(path.name.casefold())
    return result

def identity(path, entry, code):
    if not path.is_file() or path.is_symlink() or path.stat().st_size != entry['bytes'] or sha256(path) != entry['sha256']:
        raise ValueError(code+':'+str(path))

def verify_archive(archive, manifest):
    archive = Path(archive)
    expected = members(manifest)
    identity(archive, manifest['archive'], 'ARCHIVE_IDENTITY')
    with zipfile.ZipFile(archive) as source:
        files = [entry for entry in source.infolist() if not entry.is_dir()]
        names = [entry.filename for entry in files]
        if len(names) != len(set(name.casefold() for name in names)):
            raise ValueError('DUPLICATE_ARCHIVE_MEMBER')
        if set(names) != set(expected):
            raise ValueError('MEMBER_SET')
        for entry in files:
            want = expected[entry.filename]
            if entry.file_size != want['bytes'] or entry.flag_bits & 1 or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('MEMBER_IDENTITY:'+entry.filename)
            digest = hashlib.sha256()
            with source.open(entry) as stream:
                for block in iter(lambda: stream.read(1024*1024), b''):
                    digest.update(block)
            if digest.hexdigest() != want['sha256']:
                raise ValueError('MEMBER_IDENTITY:'+entry.filename)
    identity(archive, manifest['archive'], 'ARCHIVE_IDENTITY')
    return {'schema_version': 1, 'kind': 'CANONICAL_PROVIDER_ARCHIVE_VERIFICATION',
            'archive': str(archive.resolve()), 'archive_sha256': manifest['archive']['sha256'],
            'member_count': len(expected), 'execution': 'NOT_RUN'}

def verify_directory(directory, manifest):
    directory = Path(directory)
    verify_archive(directory/manifest['archive']['filename'], manifest)
    for entry in members(manifest).values():
        identity(directory/PurePosixPath(entry['path']).name, entry, 'MEMBER_IDENTITY')
    allowed = {PurePosixPath(e['path']).name.casefold() for e in manifest['files']}
    allowed |= {manifest['archive']['filename'].casefold(), 'canonical-package-receipt.json'}
    if any(p.name.casefold() not in allowed or not p.is_file() for p in directory.iterdir()):
        raise ValueError('DIRECTORY_MEMBER_SET')

def prepare(archive, destination, manifest):
    archive, destination = Path(archive), Path(destination)
    if destination.exists():
        raise ValueError('DESTINATION_EXISTS')
    verification = verify_archive(archive, manifest)
    destination.mkdir(parents=True, exist_ok=False)
    # A partial failure leaves a fresh, unapproved folder; no success receipt is written.
    target = destination/manifest['archive']['filename']
    shutil.copyfile(archive, target)
    verify_archive(target, manifest)
    with zipfile.ZipFile(target) as source:
        for name in members(manifest):
            with source.open(name) as incoming, (destination/PurePosixPath(name).name).open('xb') as outgoing:
                shutil.copyfileobj(incoming, outgoing, 1024*1024)
    verify_directory(destination, manifest)
    receipt = {**verification, 'kind': 'CANONICAL_PROVIDER_PREPARED_PACKAGE',
               'archive': str(target.resolve()), 'root': str(destination.resolve()),
               'manifest_sha256': MANIFEST_SHA256, 'loaded_module_observation': 'NOT_PERFORMED',
               'qualification': False, 'members': manifest['files']}
    (destination/'CANONICAL-PACKAGE-RECEIPT.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt

def prepare_direct(archive, destination, manifest):
    """Extract the pinned NR selection and accompanying notices; no DLL loading."""
    archive, destination = Path(archive), Path(destination)
    if destination.exists():
        raise ValueError('DESTINATION_EXISTS')
    verification = verify_archive(archive, manifest)
    selected = [entry for entry in members(manifest).values()
                if PurePosixPath(entry['path']).name.casefold() == 'nvngx_dlssnr.dll'
                or PurePosixPath(entry['path']).name.casefold().endswith('.license.txt')]
    if not any(PurePosixPath(e['path']).name.casefold() == 'nvngx_dlssnr.dll' for e in selected):
        raise ValueError('NR_MEMBER_MISSING')
    destination.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as source:
        for entry in selected:
            target = destination/PurePosixPath(entry['path']).name
            with source.open(entry['path']) as incoming, target.open('xb') as outgoing:
                shutil.copyfileobj(incoming, outgoing, 1024*1024)
            identity(target, entry, 'MEMBER_IDENTITY')
    identity(archive, manifest['archive'], 'ARCHIVE_IDENTITY')
    receipt = {**verification, 'kind': 'CANONICAL_PROVIDER_DIRECT_SELECTION',
               'root': str(destination.resolve()), 'verification_scope': 'DIRECT_NR',
               'source_archive_member_count': verification['member_count'], 'member_count': len(selected),
               'manifest_sha256': MANIFEST_SHA256, 'source_archive_verified': True,
               'selection_verified': True, 'package_verified': False,
               'dependency_completeness_established': False,
               'loaded_module_observation': 'NOT_PERFORMED', 'qualification': False,
               'members': selected}
    (destination/'CANONICAL-PACKAGE-RECEIPT.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    verify = sub.add_parser('verify'); verify.add_argument('--archive', type=Path, required=True)
    stage = sub.add_parser('prepare'); stage.add_argument('--archive', type=Path, required=True); stage.add_argument('--output', type=Path, required=True)
    direct = sub.add_parser('prepare-direct'); direct.add_argument('--archive', type=Path, required=True); direct.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = load_manifest()
    if args.command == 'verify':
        result = verify_archive(args.archive, manifest)
    else:
        operation = prepare_direct if args.command == 'prepare-direct' else prepare
        result = operation(args.archive, args.output, manifest)
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
