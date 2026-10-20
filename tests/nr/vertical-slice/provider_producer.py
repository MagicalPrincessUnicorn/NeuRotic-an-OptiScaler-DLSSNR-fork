"""Authenticate owned L12 WARP capacity campaign plus its C03 positive control.

The supported provider contract is a controlled fixture. The original L12 unit
and mutation axes stay CPU while the physical substrate is explicitly WARP.
"""
from __future__ import annotations
import ast
import dataclasses
import hashlib
import json
import pathlib
import re

from controlled_producer import Reader, need, sha, supervised
from construction_producer import ROOT, VS, MSVC, CMD, HELPER, stage, verify_test_rows, compile_command, developer_environment_script
from supervised_producer import _FRAMING

DRIVER = 'tests/nr/spectre/run_provider_lifetime.py'
OWNER = 'OptiScaler/dlssnr/NativeProviderLifetime.h'
GPU = 'OptiScaler/dlssnr/NrGpuSafety.cpp'
LEASE = 'OptiScaler/nr/lifecycle/ConsumptionLeaseAdapter.h'
CAPACITY = ('free provider slots admit exactly 64 entered recording uses',
    'full unresolved provider table refuses before another recording',
    'capacity refusal preserves every older unresolved registration',
    'only actual reset and matching receipts release full provider table',
    'genuinely terminal provider table admits a fresh exact registration')


def definitions():
    # Decode only the reviewed declarative literal list. Never execute/import
    # the CLI driver or let an external receipt supply mutation instructions.
    tree = ast.parse((ROOT / DRIVER).read_text(encoding='utf-8-sig'))
    names = {'owner': OWNER, 'gpu': GPU, 'lease': LEASE}
    def literal(node):
        if isinstance(node, ast.Name):
            return names[node.id]
        if isinstance(node, (ast.Tuple, ast.List)):
            return [literal(value) for value in node.elts]
        return ast.literal_eval(node)
    cases = []
    detectors = {}
    for node in tree.body:
        if isinstance(node, ast.If) and isinstance(node.test, ast.Attribute) and node.test.attr == 'mutations':
            for statement in node.body:
                if isinstance(statement, ast.AugAssign) and isinstance(statement.target, ast.Name) and statement.target.id == 'cases':
                    cases = literal(statement.value)
        if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == 'DETECTORS' for target in node.targets):
            detectors = ast.literal_eval(node.value)
    need(len(cases) == 11 and len({case[0] for case in cases}) == 11 and set(detectors) == {case[0] for case in cases},
         'exact reviewed eleven provider mutations required')
    return {name: (target, changes, detectors[name]) for name, target, changes in cases}


PROVIDER_MUTATIONS = definitions()
PROVIDER_BINDINGS = {ident: dict(suite='provider-capacity-composite',
    test_binding='L12.' + ('mutation' if mutant else 'unit') + '.entered-capacity/v1', assertion_ids=CAPACITY,
    physical_substrate='WARP_CONTROLLED_PROVIDER_CONTRACT', mutant=mutant,
    **(dict(mutation_names=tuple(PROVIDER_MUTATIONS), control_obligation_id='L12:unit:CPU') if mutant else {}))
    for ident, mutant in (('L12:unit:CPU', False), ('L12:mutation:CPU', True))}


def capacity_rows(raw):
    pairs = re.findall(r'^CAPACITY (PASS|FAIL) (.+)$', raw, re.M)
    need(len(pairs) == len(CAPACITY) and {name for _, name in pairs} == set(CAPACITY), 'exact five emitted capacity rows required')
    return {name: status == 'PASS' for status, name in pairs}


def adapter(raw):
    rows = re.findall(r'^WARP_ADAPTER name=.+ vendor=5140 device=140 luid_low=\d+ luid_high=-?\d+$', raw, re.M)
    need(len(rows) == 1, 'actual software adapter observation missing')
    return rows[0]


@dataclasses.dataclass(frozen=True)
class TrustedProviderCampaign:
    authority_identity: str
    origin: str
    run_directory: pathlib.Path
    repository: pathlib.Path
    run_id: str
    source_commit: str
    source_tree: str
    source_hashes: tuple[tuple[str, str], ...]
    tool_hashes: tuple[tuple[str, str], ...]
    environment_identity: str
    driver_identity: str
    config_identity: str
    artifact_hashes: tuple[tuple[str, str], ...]


def nested(record, command, cwd, timeout, expected, helper):
    supervised(record, expected)
    need(record['command'] == command and record['cwd'] == str(cwd) and record['timeout_seconds'] == timeout and
         record['supervisor_source_sha256'] == helper, 'nested original command/deadline/imported supervisor differs')


def check_overlay(reader, trust, name):
    directory = reader.root / 'provider-run' / name
    target, changes, _ = PROVIDER_MUTATIONS.get(name, (None, (), None))
    copied = {OWNER, GPU, 'OptiScaler/dlssnr/NrGpuSafety.h', LEASE, 'OptiScaler/nr/lifecycle/SameRecordingUse.h'}
    if target == LEASE:
        copied.update(path.relative_to(trust.repository).as_posix() for path in (trust.repository / 'OptiScaler/nr').rglob('*.h'))
    for path in copied:
        expected = (trust.repository / path).read_text()
        if path == target:
            for before, after in changes:
                need(before != after and expected.count(before) == 1, 'provider mutation anchor missing/ambiguous')
                expected = expected.replace(before, after)
        need(reader.path('provider-run/' + name + '/' + path).read_text() == expected, 'provider overlay is not exact reviewed substitution')
    actual = {path.relative_to(directory).as_posix() for path in (directory / 'OptiScaler').rglob('*') if path.is_file()}
    need(actual == copied, 'unverified provider overlay dependency')
    source = trust.repository / ('tests/nr/vertical-slice/native_same_recording.cpp' if target == LEASE else 'tests/nr/spectre/provider_lifetime.cpp')
    def relocate(match):
        path = (source.parent / match[1]).resolve()
        if path == trust.repository / GPU:
            path = directory / GPU
        return '#include "' + path.as_posix() + '"'
    compiled = reader.path('provider-run/' + name + '/test.cpp')
    need(compiled.read_text() == re.sub(r'#include "([^"]+)"', relocate, source.read_text()), 'comparison oracle changed in provider mutant')
    return source, compiled


def verify_provider_binding(root, result, obligation, trusted_runs, *, allow_test_only=False):
    out = dict(verified=False, product_obligation_proven=False, reason='', origin=None,
        suite='provider-capacity-composite', assertion_ids=[], physical_substrate='WARP_CONTROLLED_PROVIDER_CONTRACT')
    try:
        producer = result['producer']; trust = trusted_runs.get(producer['identity'])
        need(producer['kind'] == 'ACTUAL_RUNNER' and type(trust) is TrustedProviderCampaign and
             trust.authority_identity == producer['identity'], 'no owned provider campaign capability')
        need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'), 'test-only provider capability refused')
        root = pathlib.Path(root).resolve()
        need(root.samefile(trust.run_directory) and not trust.run_directory.is_symlink(), 'copied provider campaign')
        mapping = PROVIDER_BINDINGS[obligation['id']]
        need(result['mutant'] == mapping['mutant'] and result['test_binding'] == mapping['test_binding'], 'wrong L12 original layer')
        reader = Reader(root, trust, result['artifacts'])
        binding = reader.document(producer['authenticated_binding_ref'])
        need(binding['schema_version'] == 'NeuRotic.ProviderCampaignBinding/1' and binding['run_id'] == trust.run_id,
             'provider binding identity differs')
        for field in ('source_commit', 'source_tree', 'environment_identity', 'driver_identity', 'config_identity'):
            need(binding[field] == getattr(trust, field) == result['identities'][field] == obligation['identities'][field], 'stale provider source/tool/environment')
        pins, tools = dict(trust.source_hashes), dict(trust.tool_hashes)
        need(pins and tools and reader.document('sources-before.json') == pins == reader.document('sources-after.json') and
             reader.document('tools.json') == tools and all(sha(trust.repository / name) == value for name, value in pins.items()) and
             all(sha(pathlib.Path(name)) == value for name, value in tools.items()), 'provider source/tool drift')
        # Same source and environment issuer executes the C03 positive control,
        # then the original twelve-case provider driver in its owned child.
        stage(reader, trust, 'setup', [str(CMD), '/d', '/c', str(reader.path('environment.cmd'))], 0)
        need(reader.path('environment.cmd').read_text() == developer_environment_script(VS / 'VC/Auxiliary/Build/vcvarsall.bat'), 'setup script differs')
        suite = 'native-same-recording'; directory = root / 'baseline'
        stage(reader, trust, 'baseline/' + suite + '-compile', compile_command(suite, suite, directory, trust.repository), 0,
              maximum_timeout=1800)
        binary = reader.path('baseline/' + suite + '.exe')
        log, _ = stage(reader, trust, 'baseline/' + suite + '-test', [str(binary), trust.run_id], 0, binary=True)
        command = [binding['python'], '-B', str(trust.repository / 'tests/nr/vertical-slice/verify_construction.py'),
            '--suite', suite, '--log', str(log), '--run-dir', str(directory), '--run-id', trust.run_id]
        verified, _ = stage(reader, trust, 'baseline/' + suite + '-verify', command, 0)
        positive, _ = verify_test_rows(suite, suite, log, directory, trust.run_id, json.loads(verified.read_text()))
        need(positive.get('SP03.reserved-not-active') is True and all(positive.values()), 'C03 mutant has no exact positive control')
        command = [binding['python'], '-B', str(trust.repository / DRIVER), '--mutations', '--output-parent', str(root / 'provider-run')]
        stage(reader, trust, 'provider-launch', command, 0, maximum_timeout=3600)
        for file in ('source-before.json', 'source-after.json'):
            need(reader.document('provider-run/' + file) == {name: value for name, value in pins.items()
                 if name != 'OptiScaler/library/detours/detours.lib'}, 'provider inner source epoch differs')
        environment = reader.document('provider-run/environment.json')
        need(environment['VCTOOLSVERSION'].strip('\\/') == '14.44.35207' and environment['VSCMD_ARG_TGT_ARCH'] == 'x64', 'provider compiler environment differs')
        setup = reader.document('provider-run/environment-result.json')
        nested(setup['supervision'], ['cmd.exe', '/d', '/c', str(root / 'provider-run/environment.cmd')], trust.repository, 60, 0, pins[HELPER])
        need(setup['exit_code'] == 0 and reader.path('provider-run/environment.cmd').read_text() ==
             developer_environment_script(VS / 'VC/Auxiliary/Build/vcvarsall.bat'), 'provider setup differs')
        receipts = reader.document('provider-run/receipt.json')
        need([value['case'] for value in receipts] == ['baseline', *PROVIDER_MUTATIONS], 'provider campaign incomplete')
        driver_identity = None; baseline_sha = None
        for receipt in receipts:
            name = receipt['case']; location = root / 'provider-run' / name
            source, compiled = check_overlay(reader, trust, name)
            compiler = pathlib.Path(environment['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
            need(compiler.samefile(MSVC / 'cl.exe'), 'provider used another compiler')
            command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/Y-', '/UNDEBUG',
                '/I' + str(location / 'OptiScaler'), '/I' + str(trust.repository / 'OptiScaler'), '/I' + str(trust.repository / 'OptiScaler/include'),
                str(compiled), '/Fe' + str(location / 'test.exe'), '/Fo' + str(location / 'test.obj'), '/link',
                '/LIBPATH:' + str(trust.repository / 'OptiScaler/library/detours'), 'd3d12.lib', 'dxgi.lib', 'ole32.lib', 'detours.lib', '/STACK:8388608']
            need(receipt['compile_command'] == command and receipt['compile_exit'] == 0 and
                 receipt['compiler_sha256'] == receipt['compiler_sha256_after'] == sha(compiler) and
                 receipt['source_sha256'] == sha(source) and receipt['compiled_source_sha256'] == sha(compiled) and
                 receipt['source_unchanged'] is True and receipt['campaign_complete'] is True, 'provider compile/source mismatch')
            nested(receipt['compile_supervision'], command, trust.repository, 1800, 0, pins[HELPER])
            command = [str(reader.path('provider-run/' + name + '/test.exe'))]
            is_c03 = name == 'c03_reservation_unavailable'
            if is_c03:
                need(len(receipt['test_command']) == 2 and re.fullmatch('[0-9a-f]{32}', receipt['test_command'][1]), 'C03 run namespace missing')
                command.append(receipt['test_command'][1])
            expected_exit = 0 if name == 'baseline' else 1
            need(receipt['test_command'] == command and receipt['test_exit'] == expected_exit and
                 receipt['binary_sha256'] == sha(pathlib.Path(command[0])) and receipt['assertion_protocol_valid'] is True,
                 'provider execute result differs')
            nested(receipt['test_supervision'], command, trust.repository, 120, expected_exit, pins[HELPER])
            stdout = reader.path('provider-run/' + name + '/test.stdout.log')
            raw = stdout.read_text(); errors = reader.path('provider-run/' + name + '/test.stderr.log').read_text()
            need(raw + errors == receipt['output'] == reader.path('provider-run/' + name + '/test.log').read_text(), 'provider original captures differ')
            compile_raw = reader.path('provider-run/' + name + '/compile.stdout.log').read_text() + reader.path('provider-run/' + name + '/compile.stderr.log').read_text()
            need(compile_raw == reader.path('provider-run/' + name + '/compile.log').read_text(), 'compile captures differ')
            if is_c03:
                import contextlib, io
                inventory = json.loads((trust.repository / 'tests/nr/vertical-slice/native_same_recording_inventory.json').read_text())
                with contextlib.redirect_stdout(io.StringIO()):
                    _FRAMING.verify_log(inventory, suite, stdout, location, command[1], False)
                rows = [json.loads(line) for line in raw.splitlines()]
                failed = [row['id'] for row in rows if row.get('kind') == 'assertion' and row['matched'] is False]
            else:
                observation = adapter(raw)
                driver_identity = driver_identity or observation
                need(observation == driver_identity, 'provider adapter changed within campaign')
                rows = capacity_rows(raw)
                summaries = re.findall(r'^Provider lifetime: (\d+) checks, (\d+) failures; WARP lifecycle plumbing only; proprietary provider NOT_RUN$', raw, re.M)
                failed = re.findall(r'^FAIL (.+)$', errors, re.M)
                need(len(summaries) == 1 and int(summaries[0][1]) == len(failed) and int(summaries[0][0]) > 0, 'provider summary incomplete')
                if name == 'baseline':
                    need(int(summaries[0][0]) == 22570 and not failed and all(rows.values()), 'capacity positive control missing')
                    baseline_sha = receipt['binary_sha256']
            need(failed == receipt['failed_assertions'] and receipt['result'] == ('PASS' if name == 'baseline' else 'KILLED') and
                 (name == 'baseline' or PROVIDER_MUTATIONS[name][2] in failed), 'mutant did not fail intended semantic oracle')
        need(trust.environment_identity == 'sha256:' + sha(reader.path('environment.json')) and
             trust.driver_identity == 'WARP:sha256:' + hashlib.sha256(driver_identity.encode()).hexdigest(), 'provider environment/adapter pin differs')
        primary = receipts[-1]['binary_sha256'] if result['mutant'] else baseline_sha
        need(primary == result['identities']['binary_sha256'] == obligation['identities']['binary_sha256'], 'provider binary identity differs')
        out.update(verified=True, origin=trust.origin, source_files=pins, assertion_ids=list(CAPACITY),
            campaign_id=trust.run_id, campaign_kind='PROVIDER', baseline_binary_sha256=baseline_sha,
            mutation_names=list(PROVIDER_MUTATIONS) if result['mutant'] else [],
            reason='Actual WARP entered capacity/refusal/retirement plus eleven intended semantic mutants and separate exact C03 positive control; supported provider contract remains synthetic.')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, OverflowError) as error:
        out['reason'] = str(error)
    return out
