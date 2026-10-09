"""Native shipping composition; research builders retain their source inputs.

This acts on an in-memory package, never an installed game. Shared native NR,
FG, model, Pix and Inspector payloads remain independent of ReShade addons.
"""
from pathlib import PurePosixPath


def retired_member(name):
    path = name.replace('\\', '/').casefold()
    leaf = PurePosixPath(path).name
    retired_roots = ('packages/prepared-x86/', 'optional/x86/',
                     'optional/reshade/', 'optional/externalfeeder/')
    retired_folders = ('/optiscaler/preparedguides/', '/neurotic/prepared/',
                       '/neurotic.gpuhost/', '/reshade-shaders/', '/drme/')
    retired_support = {'preparedsetup.ps1', 'preparedprofile.ps1',
                       'start-preparedtest.ps1', 'start-x86-prepared.ps1',
                       'start-x86-prepared.cmd', 'reshade-setup.cmd',
                       'start-prepared-test.cmd', 'restore-test-settings.cmd',
                       'neurotic-hubhistory.ps1', 'neurotic-hubsanitize.ps1',
                       'neurotic-hubdumbfire.ps1', 'neurotic-connectiondependencies.ps1',
                       'universalconnectiondependencies.json'}
    return (path.startswith(retired_roots) or any(part in path for part in retired_folders)
            or leaf in retired_support or leaf in {'reshade32.dll', 'reshade64.dll', 'reshade.ini'}
            or leaf.startswith('reshade_setup_') or leaf.endswith(('.addon32', '.addon64')))


def retain_native_shipping(files):
    for name in list(files):
        if retired_member(name):
            del files[name]
    return files


def require_native_shipping(files):
    retired = [name for name in files if retired_member(name)]
    if retired:
        raise ValueError('Retired ReShade/prepared shipping payload: ' + ', '.join(retired[:8]))
