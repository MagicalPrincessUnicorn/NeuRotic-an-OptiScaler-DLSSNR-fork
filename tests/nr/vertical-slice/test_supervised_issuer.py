"""Construction checks for the trusted invocation; these are not live evidence."""
import pathlib
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import run_supervised_producer as issuer
import gate_evaluator as evaluator


class SupervisedIssuerTests(unittest.TestCase):
    def test_n08_requires_both_order_controls_and_the_actual_simultaneous_race(self):
        for key in ('N08:unit:CPU', 'N08:state-machine:CPU'):
            mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS[key]
            self.assertTrue(mapping['source_sha256'])
            for name, value in mapping['source_sha256'].items():
                self.assertEqual(issuer.digest(issuer.ROOT / name), value, name)
            receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite=mapping['suite'],
                assertion_ids=list(mapping['assertion_ids']), source_files=dict(mapping['source_sha256']))
            result = dict(test_binding=mapping['test_binding'], mutant=False)
            self.assertTrue(evaluator.spectre_runner_covers(key, receipt, result))
            for ident in mapping['assertion_ids']:
                self.assertFalse(evaluator.spectre_runner_covers(key,
                    dict(receipt, assertion_ids=[item for item in mapping['assertion_ids'] if item != ident]), result))
            self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, origin='TEST_ONLY'), result))

    def test_outer_launcher_timeout_or_incomplete_cleanup_never_enrolls(self):
        for code, infra, empty, capture in ((124, True, True, True), (125, True, False, False),
                                           (0, False, False, True), (0, False, True, False)):
            with self.subTest(code=code, empty=empty), tempfile.TemporaryDirectory() as temporary:
                output = pathlib.Path(temporary)
                process = SimpleNamespace(returncode=code, infrastructure_error=infra,
                    supervision=dict(group_empty_observed=empty, capture_complete=capture))
                with patch.object(issuer, 'owned_run', return_value=process), self.assertRaises(ValueError):
                    issuer.run_existing_runner(output, pathlib.Path(sys.executable), 60)
                self.assertTrue((output / 'host-launch-result.json').is_file())

    def test_n04_reviewed_closure_contains_all_repository_include_dependencies(self):
        mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N04:unit:CPU']
        manifests = list(pathlib.Path(__file__).parent.glob('*_reviewed_source_closure.json'))
        self.assertEqual(len(manifests), 6)
        documents = [json.loads(path.read_bytes()) for path in manifests]
        for document in documents:
            self.assertEqual(document['source_sha256'], mapping['source_sha256'])
        pending = [issuer.ROOT / name for document in documents for name in document['entry_points']]
        seen = set()
        while pending:
            path = pending.pop().resolve()
            if path in seen:
                continue
            seen.add(path)
            self.assertIn(path.relative_to(issuer.ROOT).as_posix(), mapping['source_sha256'])
            for name in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',
                                   path.read_text(), re.MULTILINE):
                found = next((p.resolve() for p in (path.parent / name, issuer.ROOT / 'OptiScaler' / name,
                             issuer.ROOT / 'OptiScaler/include' / name) if p.is_file()), None)
                if found:
                    pending.append(found)

    def test_unpinned_or_missing_manifest_cannot_enroll_source_review(self):
        self.assertEqual(evaluator._reviewed_source_closure('n04_reviewed_source_closure.json', '0' * 64), {})
        self.assertEqual(evaluator._reviewed_source_closure('absent-reviewed-closure.json', '0' * 64), {})
        manifest = pathlib.Path(__file__).parent / 'n04_reviewed_source_closure.json'
        raw = manifest.read_bytes()
        document = json.loads(raw)
        review_path = issuer.ROOT / document['review_record']['path']
        read_bytes = pathlib.Path.read_bytes
        for replacement in (b'{}', read_bytes(review_path) + b'\n'):
            with self.subTest(tampered_review=replacement[:20]):
                def changed_bytes(path):
                    return replacement if path == review_path else read_bytes(path)
                with patch.object(pathlib.Path, 'read_bytes', changed_bytes):
                    self.assertEqual(evaluator._reviewed_source_closure(manifest.name,
                        hashlib.sha256(raw).hexdigest()), {})
        review = json.loads(read_bytes(review_path))
        for replacement in (dict(review, source_sha256={}), dict(review, decision='PENDING_REVIEW')):
            with self.subTest(inconsistent_review=replacement['decision']):
                review_raw = json.dumps(replacement).encode()
                changed = dict(document, review_record=dict(document['review_record'],
                    sha256=hashlib.sha256(review_raw).hexdigest()))
                manifest_raw = json.dumps(changed).encode()
                def inconsistent_bytes(path):
                    if path == review_path:
                        return review_raw
                    if path == manifest.resolve():
                        return manifest_raw
                    return read_bytes(path)
                with patch.object(pathlib.Path, 'read_bytes', inconsistent_bytes):
                    self.assertEqual(evaluator._reviewed_source_closure(manifest.name,
                        hashlib.sha256(manifest_raw).hexdigest()), {})

    def test_n04_rejects_unreviewed_oracle_and_transitive_dependency_bytes(self):
        mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N04:unit:CPU']
        # A real issuer may honestly enroll a new source tree. That establishes
        # provenance, not review of a changed comparison oracle or dependency.
        dependencies = (
            'tests/nr/contracts/Test002Support.h',
            'tests/nr/contracts/test002_source_and_evidence.py',
            'tests/nr/contracts/NativeProcessGroup.ps1',
            'tests/nr/contracts/CommandEvidence.ps1',
            'tests/nr/context/ContextFixtureStore.h',
            'OptiScaler/nr/lifecycle/NativeSessionLifetime.cpp',
            'OptiScaler/nr/protocol/ProfileQualification.h',
            'OptiScaler/dlssnr/NativeProviderLifetime.h',
            'OptiScaler/nr/lifecycle/OpaqueSrEvaluationReceipt.h',
            'OptiScaler/dlssnr/NativeDispatchOutcome.h',
            'tests/nr/vertical-slice/supervised_producer.py',
            'tests/nr/vertical-slice/run_supervised_producer.py',
        )
        original = dict(mapping['source_sha256'])
        original.update((name, issuer.digest(issuer.ROOT / name)) for name in dependencies)
        result = dict(test_binding=mapping['test_binding'], mutant=False)
        receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite=mapping['suite'],
                       assertion_ids=list(mapping['assertion_ids']), source_files=original)
        self.assertTrue(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result))
        for name in dependencies:
            with self.subTest(dependency=name):
                receipt['source_files'] = dict(original, **{name: '0' * 64})
                self.assertFalse(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result),
                                 'unreviewed dependency cannot inherit the reviewed oracle: ' + name)
        name = 'OptiScaler/dlssnr/NativeParameterOverride.h'
        raw = (issuer.ROOT / name).read_bytes()
        changed_eol = raw.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        self.assertNotEqual(raw, changed_eol)
        receipt['source_files'] = dict(original, **{name: hashlib.sha256(changed_eol).hexdigest()})
        self.assertFalse(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result))

    def test_n04_reviewed_files_are_exact_current_source(self):
        mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N04:unit:CPU']
        for name, expected in mapping['source_sha256'].items():
            self.assertEqual(issuer.digest(issuer.ROOT / name), expected, name)

    def test_n04_unit_requires_exact_group_and_reviewed_source(self):
        mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS.get('N04:unit:CPU')
        self.assertIsNotNone(mapping, 'reviewed N04 unit oracle mapping is required')
        receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite=mapping['suite'],
                       assertion_ids=list(mapping['assertion_ids']), source_files=dict(mapping['source_sha256']))
        result = dict(test_binding=mapping['test_binding'], mutant=False)
        self.assertTrue(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result))
        self.assertFalse(evaluator.spectre_runner_covers('N04:integration:CPU', receipt, result))
        for ident in mapping['assertion_ids']:
            receipt['assertion_ids'] = [item for item in mapping['assertion_ids'] if item != ident]
            self.assertFalse(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result), ident)
        receipt['assertion_ids'] = list(mapping['assertion_ids'])
        receipt['origin'] = 'TEST_ONLY'
        self.assertFalse(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result))
        receipt['origin'] = 'ACTUAL_RUNNER'
        first = next(iter(receipt['source_files']))
        receipt['source_files'][first] = '0' * 64
        self.assertFalse(evaluator.spectre_runner_covers('N04:unit:CPU', receipt, result))

    def test_git_tree_encoding_matches_existing_git_tree(self):
        records = issuer.git('ls-tree', '-r', '-z', 'HEAD').split(b'\0')
        entries = []
        for record in records:
            if record:
                header, name = record.split(b'\t', 1)
                mode, _, object_id = header.decode().split()
                entries.append((name.decode(), mode, object_id))
        self.assertEqual(issuer.git_tree(entries), issuer.git('rev-parse', 'HEAD^{tree}').decode().strip())
        self.assertEqual(issuer.git_tree([]), '4b825dc642cb6eb9a060e54bf8d69288fbee4904')

    @unittest.skipUnless(issuer.POWERSHELL.is_file(), 'Windows Desktop PowerShell parser required')
    def test_generated_launch_script_parses_without_executing_runner(self):
        with tempfile.TemporaryDirectory(prefix='issuer script ') as temporary:
            output = pathlib.Path(temporary)
            command, owned, environment = issuer.prepare_launcher(output, pathlib.Path(sys.executable), 60)
            script = output / 'host-launch.ps1'
            text = script.read_text()
            self.assertNotIn('@@', text)
            self.assertIn('HOST_CAPTURED_CPU_ENVIRONMENT', text)
            self.assertEqual(owned.parent, output)
            self.assertEqual(environment.parent, output)
            self.assertEqual(command[0], str(issuer.CMD))
            expression = "$parseTokens=$null; $parseErrors=$null; [void][System.Management.Automation.Language.Parser]::ParseFile(" + issuer.ps_quote(script) + ",[ref]$parseTokens,[ref]$parseErrors); if($parseErrors.Count){$parseErrors|Out-String|Write-Error;exit 1}"
            checked = subprocess.run([str(issuer.POWERSHELL), '-NoLogo', '-NoProfile', '-NonInteractive',
                                      '-Command', expression], capture_output=True, timeout=30)
            self.assertEqual(checked.returncode, 0, checked.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()
