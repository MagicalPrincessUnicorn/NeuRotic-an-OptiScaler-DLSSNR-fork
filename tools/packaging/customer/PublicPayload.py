"""Keep runtime contracts in the customer payload and build evidence outside it."""
import hashlib
import json
from InspectorLayout import component

PACKAGE = 'packages/current/'
WORKER = PACKAGE + 'payload/' + component()['relativeRoot'] + '/'
REMOVED = frozenset({
    'ARCHIVE-MANIFEST.json', 'COMPONENTS.json', 'CHANGES.txt', 'INSTALL-CHECKLIST.txt',
    'KNOWN-ISSUES.txt', 'START HERE.txt', PACKAGE + 'README.txt',
    PACKAGE + 'support/BUILD-MANIFEST.json',
})
ROOT_FILES = frozenset({'README.txt', 'LICENSE', 'THIRD-PARTY-NOTICES.txt',
    'NeuRotic.exe', 'NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd'})
DATASET_KEYS = ('schema_version', 'dataset_id', 'review_date', 'platform', 'counts', 'policy', 'games')
COUNT_KEYS = ('game_family_records', 'game_alias_records', 'unique_basename_only_warning_aliases',
              'unique_context_required_aliases', 'steam_app_identities')
GAME_KEYS = ('id', 'title', 'identity_match_enabled', 'runtime_verified', 'action',
             'steam_app_ids', 'executables', 'anti_cheat_reported')
ALIAS_KEYS = ('basename', 'active_warning_rule', 'match_mode')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode('utf-8')


def rows(files, *, windows=False):
    return [{'path': n.replace('/', '\\') if windows else n, 'bytes': len(raw), 'sha256': sha(raw)}
            for n, raw in sorted(files.items())]


def pick(value, keys):
    # Missing consumer fields are an error, never invented defaults.
    return {key: value[key] for key in keys}


def public_dataset(value):
    result = pick(value, DATASET_KEYS)
    result['counts'] = pick(value['counts'], COUNT_KEYS)
    result['policy'] = pick(value['policy'], ('action', 'no_match_means'))
    result['games'] = []
    for game in value['games']:
        clean = pick(game, GAME_KEYS)
        clean['executables'] = [pick(alias, ALIAS_KEYS) for alias in game['executables']]
        result['games'].append(clean)
    return result


def public_payload(source, documents):
    """Return a fresh payload plus separate complete verification evidence.

    The Inspector cohort, provider digest guard and installer inventory remain
    enforced. Originals are retained in the evidence for independent review.
    """
    files = dict(source)
    evidence = {
        'schema': 1,
        'source_archive_manifest': json.loads(source['ARCHIVE-MANIFEST.json']),
        'components': json.loads(source['COMPONENTS.json']),
        'build': json.loads(source[PACKAGE + 'support/BUILD-MANIFEST.json']),
        'package': json.loads(source[PACKAGE + 'support/PACKAGE-MANIFEST.json']),
        'distribution': json.loads(source[WORKER + 'DISTRIBUTION-MANIFEST.json']),
        'warning_data': {},
    }
    for name in REMOVED:
        files.pop(name, None)
    files.update(documents)
    unexpected = {name for name in files if '/' not in name} - ROOT_FILES
    if unexpected:
        raise ValueError('Unexpected public root files: ' + repr(sorted(unexpected)))
    for prefix in ('support/', PACKAGE + 'support/'):
        name = prefix + 'NeuRotic-AntiCheatProviders.json'
        original = source[name]
        provider = json.loads(original)
        evidence['warning_data'][name] = provider
        provider = {**provider, 'notes': []}
        files[name] = encode(provider)
        script = prefix + 'NeuRotic-HubAntiCheat.ps1'
        old_pin = sha(original).encode('ascii')
        if source[script].count(old_pin) != 1:
            raise ValueError('Provider integrity guard differs: ' + script)
        files[script] = source[script].replace(old_pin, sha(files[name]).encode('ascii'))
        name = prefix + 'neurotic_anticheat_rules.json'
        dataset = json.loads(source[name])
        evidence['warning_data'][name] = dataset
        files[name] = encode(public_dataset(dataset))
    # The host authenticates the files array. Leave every cohort member intact.
    files[WORKER + 'DISTRIBUTION-MANIFEST.json'] = encode({'files': evidence['distribution']['files']})
    manifest_name = PACKAGE + 'support/PACKAGE-MANIFEST.json'
    manifest = pick(evidence['package'], ('kind', 'version', 'loggingProfile', 'components'))
    manifest['name'] = 'NeuRotic'
    manifest['files'] = rows({n[len(PACKAGE):]: raw for n, raw in files.items()
                              if n.startswith(PACKAGE) and n != manifest_name}, windows=True)
    files[manifest_name] = encode(manifest)
    registry = json.loads(source['support/Hub-Packages.json'])
    if len(registry['packages']) != 1 or registry['packages'][0]['id'] != 'flagship-approved':
        raise ValueError('Unexpected Hub package registry')
    registry['packages'][0]['name'] = 'NeuRotic'
    registry['packages'][0]['manifestSha256'] = sha(files[manifest_name])
    files['support/Hub-Packages.json'] = encode(registry)
    evidence['inventory'] = {'source_commit': evidence['source_archive_manifest']['source_commit'],
                             'files': rows(files)}
    evidence['removed'] = sorted(set(source) - set(files))
    evidence['changed'] = sorted(name for name in files if source.get(name) != files[name])
    return files, evidence
