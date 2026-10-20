"""Authenticate original C++/compile-time construction oracles from owned runs.

Only a trusted invocation creates TrustedConstructionRun in memory. A source
registration or saved receipt cannot enroll itself or establish reviewed coverage.
"""
from __future__ import annotations
import contextlib
import dataclasses
import hashlib
import io
import json
import pathlib
import re
import sys

from construction_profiles import (PROFILES, COMPOSITES, COMPILE_PROFILE, COMPILE_SOURCE,
    COMPILE_CASES, COMPILE_ASSERTIONS, MUTATIONS, CONSTRUCTION_BINDINGS, members, roots)
from controlled_producer import Reader, need, sha, supervised
from supervised_producer import _FRAMING

ROOT = pathlib.Path(__file__).resolve().parents[3]
VS = pathlib.Path(r'C:\Program Files\Microsoft Visual Studio\18\Community')
MSVC = VS / 'VC/Tools/MSVC/14.44.35207/bin/HostX64/x64'
CMD = pathlib.Path(r'C:\Windows\System32\cmd.exe')
HELPER = 'tests/nr/spectre/owned_process.py'
sys.path.insert(0, str(ROOT / 'tests/nr/spectre'))
from owned_process import developer_environment_script


def classify_comparisons(rows, exit_code, detectors=()):
    if (not rows or any(type(value) is not bool for value in rows.values()) or
            exit_code not in (0, 1) or (exit_code == 0) != all(rows.values())):
        return 'INFRA_ERROR'
    if not detectors:
        return 'PASS' if exit_code == 0 else 'FAIL'
    return 'KILLED' if all(rows.get(name) is False for name in detectors) else 'SURVIVED'


def verify_stage_intent(intent, supervision):
    need(all(supervision.get(key) == intent[key] for key in ('command', 'cwd', 'timeout_seconds')),
         'original supervision differs from command intent')


def verify_overlay_membership(declared, expected, actual):
    need(isinstance(declared, list) and len(declared) == len(set(declared)) and
         set(declared) == set(expected) == set(actual), 'overlay must contain the entire exact enrolled dependency set')


def substitute(original, before, after):
    old, new = before.encode('utf-8'), after.encode('utf-8')
    need(old and old != new and original.count(old) == 1, 'source substitution must be unique and effectful')
    return original.replace(old, new)


def compiler_oracle(case, source, text):
    _, code, symbol, statement = COMPILE_CASES[case]
    lines = source.read_text(encoding='utf-8-sig').splitlines()
    numbers = [i + 1 for i, line in enumerate(lines) if statement in line]
    if len(numbers) != 1:
        return False
    pattern = r'^' + re.escape(str(source)) + r'\(' + str(numbers[0]) + r'\): error ' + code + r':.*' + re.escape(symbol)
    return bool(re.search(pattern, text, re.MULTILINE))


def compile_command(profile, suite, output, source_root, *, case=None, control=False):
    command = [str(MSVC / 'cl.exe'), '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/Y-', '/UNDEBUG',
               '/I' + str(source_root / 'OptiScaler'), '/I' + str(source_root / 'OptiScaler/include')]
    if profile == COMPILE_PROFILE:
        return [*command, '/c', '/DNR_AUTH_CASE=' + str(case),
                *(['/DNR_AUTH_CONTROL'] if control else []), str(source_root / COMPILE_SOURCE),
                '/Fo' + str(output / ('authority-' + str(case) + ('-control' if control else '-negative') + '.obj'))]
    spec = PROFILES[suite]
    return [*command, *('/D' + name for name in spec['defines']),
            *(str(source_root / name) for name in spec['sources']), '/Fe' + str(output / (suite + '.exe')),
            '/Fo' + str(output) + '\\', '/link', '/STACK:8388608', *spec['libraries']]


def qualified_rows(profile, suite, rows):
    return {((suite + '/') if profile in COMPOSITES else '') + key: value for key, value in rows.items()}


def verify_test_rows(profile, suite, log, run_directory, run_id, report):
    inventory = json.loads((ROOT / PROFILES[suite]['inventory']).read_text(encoding='utf-8-sig'))
    capture = io.StringIO()
    with contextlib.redirect_stdout(capture):
        _FRAMING.verify_log(inventory, suite, log, run_directory, run_id, False)
    framing = json.loads(capture.getvalue())
    need(framing == report, 'independent framing disagrees with captured verifier')
    declared = {row['id']: row for row in inventory['suites'][suite]}
    raw = [_FRAMING.load_json(line) for line in _FRAMING.bounded_text(log).splitlines()]
    rows = {row['id']: row for row in raw if row.get('kind') == 'assertion'}
    need(all(row['execution'] == 'COMPLETED' for key, row in rows.items()
             if declared[key]['binding'] == 'CURRENT_CALLABLE'), 'callable row did not complete')
    selected = {key: row['matched'] for key, row in rows.items()
                if declared[key]['binding'] == 'CURRENT_CALLABLE'}
    need(selected and all(type(value) is bool for value in selected.values()), 'no exact callable assertions')
    return qualified_rows(profile, suite, selected), framing


@dataclasses.dataclass(frozen=True)
class TrustedConstructionRun:
    authority_identity: str
    origin: str
    run_directory: pathlib.Path
    repository: pathlib.Path
    run_id: str
    profile: str
    source_commit: str
    source_tree: str
    source_hashes: tuple[tuple[str, str], ...]
    tool_hashes: tuple[tuple[str, str], ...]
    environment_identity: str
    config_identity: str
    artifact_hashes: tuple[tuple[str, str], ...]
    mutation_names: tuple[str, ...] = ()


def stage(reader, trust, name, expected_command, expected_exit, *, binary=False, maximum_timeout=300):
    value = reader.document(name + '.result.json')
    intent = reader.document(name + '.intent.json')
    need(value['intent'] == intent and intent['command'] == expected_command and
         intent['cwd'] == str(trust.repository) and intent['run_id'] == trust.run_id and
         0 < intent['timeout_seconds'] <= maximum_timeout, 'stage intent/command/cwd/bound mismatch')
    supervised(value['supervision'], expected_exit)
    verify_stage_intent(intent, value['supervision'])
    need(value['returncode'] == expected_exit and value['infrastructure_error'] is False and
         value['supervision'].get('supervisor_source_sha256') == dict(trust.source_hashes)[HELPER],
         'stage infrastructure or imported-supervisor mismatch')
    executable = pathlib.Path(expected_command[0]).resolve()
    expected_sha = sha(executable) if binary else dict(trust.tool_hashes).get(str(executable))
    need(expected_sha and value['executable_sha256_before'] == expected_sha ==
         value['executable_sha256_after'], 'executed tool/binary identity changed')
    if binary:
        reader.path(executable.relative_to(reader.root).as_posix())
    captures = []
    for stream in ('stdout', 'stderr'):
        path = reader.path(name + '.' + stream + '.log')
        need(value[stream + '_sha256'] == sha(path), 'original capture hash mismatch')
        captures.append(path)
    return captures


def verify_overlay(reader, trust, variant):
    root = reader.root / variant / 'overlay'
    definition = MUTATIONS[trust.profile][variant]
    changes = {}
    for name, before, after in definition['changes']:
        need(name not in changes, 'one reviewed substitution per source required')
        changes[name] = (before, after)
    declared = reader.document(variant + '/overlay-files.json')
    verify_overlay_membership(declared, dict(trust.source_hashes),
        [path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()])
    for name in declared:
        need(name in dict(trust.source_hashes), 'unreviewed overlay dependency')
        original = (trust.repository / name).read_bytes()
        expected = substitute(original, *changes[name]) if name in changes else original
        need(reader.path(variant + '/overlay/' + name).read_bytes() == expected,
             'overlay differs from exact reviewed source substitution')
    need(set(changes).issubset(declared), 'mutation source absent')
    return root


def verify_construction_binding(root, result, obligation, trusted_runs, *, allow_test_only=False):
    out = dict(verified=False, product_obligation_proven=False, reason='', origin=None,
               suite=None, assertion_ids=[], physical_substrate='CPU_CANONICAL_WITH_SYNTHETIC_EXTERIOR')
    try:
        producer = result['producer']; trust = trusted_runs.get(producer['identity'])
        need(producer['kind'] == 'ACTUAL_RUNNER' and type(trust) is TrustedConstructionRun and
             producer['identity'] == trust.authority_identity, 'no independently enrolled construction authority')
        need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'),
             'test-only construction authority refused')
        root = pathlib.Path(root).resolve()
        need(root.samefile(trust.run_directory) and not pathlib.Path(trust.run_directory).is_symlink(),
             'copied construction campaign cannot reuse authority')
        need(re.fullmatch('[a-f0-9]{32}', trust.run_id) and re.fullmatch('[a-f0-9]{40}', trust.source_commit)
             and re.fullmatch('[a-f0-9]{40}', trust.source_tree), 'malformed construction identity')
        need(obligation['axis'] in ('CPU', 'SOURCE_ANALYSIS'), 'CPU construction cannot prove physical execution')
        mapping = CONSTRUCTION_BINDINGS[obligation['id']]
        need(mapping['suite'] == trust.profile and result['mutant'] == mapping.get('mutant', False) and
             result['test_binding'] == mapping['test_binding'], 'construction profile cannot change original layer or oracle')
        reader = Reader(root, trust, result['artifacts'])
        binding = reader.document(producer['authenticated_binding_ref'])
        need(binding['schema_version'] == 'NeuRotic.ConstructionBinding/1' and
             binding['run_id'] == trust.run_id and binding['profile'] == trust.profile,
             'binding run/profile mismatch')
        for field in ('source_commit', 'source_tree', 'environment_identity', 'config_identity'):
            need(binding[field] == getattr(trust, field) == result['identities'][field] == obligation['identities'][field],
                 'stale or cross-source/environment construction identity')
        pins, tools = dict(trust.source_hashes), dict(trust.tool_hashes)
        need(pins and len(pins) == len(trust.source_hashes) and tools and len(tools) == len(trust.tool_hashes),
             'missing or duplicate source/tool pins')
        need(all(sha(trust.repository / name) == value for name, value in pins.items()), 'source changed after enrollment')
        need(all(sha(pathlib.Path(name)) == value for name, value in tools.items()), 'tool changed after enrollment')
        need(reader.document('sources-before.json') == pins == reader.document('sources-after.json') and
             reader.document('tools.json') == tools, 'source/tool capture differs from authority')
        environment = reader.path('environment.json')
        need(trust.environment_identity == 'sha256:' + sha(environment), 'environment capture differs')
        env = reader.document('environment.json')
        need(env.get('VCTOOLSVERSION', '').strip('\\/') == '14.44.35207' and
             env.get('VSCMD_ARG_TGT_ARCH') == 'x64' and not env.get('CL') and not env.get('_CL_'),
             'not the exact clean MSVC toolchain')
        setup = [str(CMD), '/d', '/c', str(reader.path('environment.cmd'))]
        need(reader.path('environment.cmd').read_text(encoding='utf-8-sig') ==
             developer_environment_script(str(VS / 'VC/Auxiliary/Build/vcvarsall.bat')),
             'developer environment setup differs from exact pinned toolchain')
        stage(reader, trust, 'setup', setup, 0)
        all_rows, binaries = {}, {}
        for variant in ('baseline', *trust.mutation_names):
            location = root / variant
            source_root = trust.repository if variant == 'baseline' else verify_overlay(reader, trust, variant)
            rows = {}
            if trust.profile == COMPILE_PROFILE:
                need(not trust.mutation_names and result['mutant'] is False, 'compiler profile has no runtime mutation authority')
                for case, (ident, *_rest) in COMPILE_CASES.items():
                    for control in (True, False):
                        label = 'authority-' + str(case) + ('-control' if control else '-negative')
                        command = compile_command(trust.profile, '', location, source_root, case=case, control=control)
                        stdout, stderr = stage(reader, trust, variant + '/' + label, command, 0 if control else 2,
                                               maximum_timeout=1800)
                        if not control:
                            need(compiler_oracle(case, source_root / COMPILE_SOURCE,
                                 stdout.read_text(encoding='utf-8-sig') + stderr.read_text(encoding='utf-8-sig')),
                                 'nonzero compiler result lacks exact intended rejection')
                    rows[ident] = True
                rows['legitimate-api-controls-compile'] = True
                binaries[variant] = sha(MSVC / 'cl.exe')
            else:
                for suite in members(trust.profile):
                    prefix = variant + '/' + suite
                    command = compile_command(trust.profile, suite, location, source_root)
                    stage(reader, trust, prefix + '-compile', command, 0, maximum_timeout=1800)
                    binary = reader.path(variant + '/' + suite + '.exe')
                    prefix_id = suite + '/' if trust.profile in COMPOSITES else ''
                    suite_fails = variant != 'baseline' and any(name.startswith(prefix_id)
                        for name in MUTATIONS[trust.profile][variant]['detectors'])
                    expected = 1 if suite_fails else 0
                    log, _ = stage(reader, trust, prefix + '-test', [str(binary), trust.run_id], expected, binary=True)
                    verification = [binding['python'], '-B', str(trust.repository / 'tests/nr/vertical-slice/verify_construction.py'),
                        '--suite', suite, '--log', str(log), '--run-dir', str(location), '--run-id', trust.run_id]
                    verified_log, _ = stage(reader, trust, prefix + '-verify', verification, 0)
                    selected, framing = verify_test_rows(trust.profile, suite, log, location, trust.run_id,
                        json.loads(verified_log.read_text(encoding='utf-8-sig')))
                    need(not set(rows).intersection(selected), 'duplicate composite assertion identity')
                    rows.update(selected)
                    need((framing['mismatches'] == 0) == (not suite_fails), 'assertion verdict differs from process exit')
                    binaries.setdefault(variant, sha(binary))
            need(rows and (all(rows.values()) if variant == 'baseline' else
                 all(rows.get(name) is False for name in MUTATIONS[trust.profile][variant]['detectors'])),
                 'missing baseline or intended semantic failure')
            all_rows[variant] = rows
        for variant in trust.mutation_names:
            need(all(all_rows['baseline'].get(name) is True for name in MUTATIONS[trust.profile][variant]['detectors']),
                 'semantic mutant lacks its exact fresh positive control')
        mutant = result['mutant']
        need(not mutant or trust.mutation_names, 'no actual mutation campaign')
        required_mutations = mapping.get('mutation_names', ()) if mutant else ()
        need(set(required_mutations).issubset(trust.mutation_names), 'required original semantic mutants did not run')
        primary = binaries[required_mutations[0] if mutant else 'baseline']
        need(primary == result['identities']['binary_sha256'] == obligation['identities']['binary_sha256'],
             'primary executable identity differs')
        out.update(verified=True, origin=trust.origin, suite=trust.profile, assertion_ids=list(all_rows['baseline']),
            source_files=pins, campaign_id=trust.run_id, mutation_names=list(required_mutations),
            baseline_binary_sha256=binaries['baseline'], campaign_kind='CONSTRUCTION',
            reason='owned exact compiler/C15 oracle and fresh semantic controls authenticated')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, OverflowError) as error:
        out['reason'] = str(error)
    return out
