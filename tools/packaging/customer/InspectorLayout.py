"""Read the host's required cohort version; relocate donor bytes without changing them."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / 'OptiScaler/nr/semantic/character/CharacterInstallLayout.h'

def required_version():
    match = re.search(r'RequiredCharacterInspectorVersion\[\]\s*=\s*L"(Version [1-9][0-9]{0,6})"', HEADER.read_text())
    if not match:
        raise ValueError('Host Inspector version constant is missing or unsupported')
    return match[1]

def component():
    version = required_version()
    return {'version': version, 'relativeRoot': 'OptiScaler/CharacterInspector/' + version}

def version_inspector(files):
    version = required_version()
    for name in list(files):
        marker = '/OptiScaler/CharacterInspector/'
        if marker not in name:
            continue
        prefix, suffix = name.split(marker, 1)
        if re.match(r'Version [1-9][0-9]{0,6}/', suffix):
            continue
        if suffix.startswith('.'):
            raise ValueError('Inspector staging cannot enter a package')
        destination = prefix + marker + version + '/' + suffix
        if destination in files:
            raise ValueError('Inspector donor collides with a versioned member')
        files[destination] = files.pop(name)
    return files
