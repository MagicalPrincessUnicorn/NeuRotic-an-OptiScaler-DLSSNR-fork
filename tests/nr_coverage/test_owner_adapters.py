"""Historical owner copies cannot mint live capability or coverage authority."""
import copy
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from tools.nr_coverage import adapters
from tools.nr_coverage.identity import canonical, digest
from tools.nr_coverage.schema import CoverageError, strict_load
from tools.nr_coverage.store import Store
from tools.nr_coverage.migration import migrate_claims

HERE = Path(__file__).parent / 'fixtures' / 'owners'
SCHEMAS = Path(__file__).parents[2] / 'tools' / 'nr_coverage' / 'schemas' / 'owners'

def source(name): return (HERE / name).read_bytes()
def obj(name): return json.loads(source(name))

class OwnerAdapterTests(unittest.TestCase):
    def api(self, name):
        value = getattr(adapters, name, None)
        self.assertTrue(callable(value), 'Missing historical owner API: ' + name)
        return value

    def test_g_candidate_preserves_unknown_identity_without_admission(self):
        raw = source('G_candidate_examples.json')
        view = self.api('owner_view')(raw, 'G', selector='/examples/0/record')
        self.assertEqual('CANDIDATE', view['reported']['local_stage'])
        self.assertEqual('unknown', view['reported']['frame_ref']['state'])
        self.assertFalse(view['reported']['may_admit'])
        self.assertFalse(view['runtime_authority'])
        envelope = self.api('owner_envelope')(raw, 'G', selector='/examples/0/record')
        self.assertEqual('/examples/0/record', envelope['record']['source']['selector'])
        self.assertEqual(digest(raw), envelope['record']['source']['raw_sha256'])
        self.assertEqual('known', envelope['cell']['binding_profiles']['G']['state'])
        self.assertTrue(all(axis['status'] == 'NOT_RUN' for axis in envelope['record']['outcomes'].values()))

    def test_i_conflicting_and_foreign_session_claims_stay_historical(self):
        raw = source('I_VALID_SNAPSHOTS.json')
        view = self.api('owner_view')(raw, 'I', selector='/snapshots/conflicting_direct')
        self.assertEqual('TEST-SESSION-A', view['reported']['origin_session'])
        self.assertGreaterEqual(len(view['reported']['facts']), 2)
        self.assertEqual('unknown', view['currentness']['state'])
        self.assertFalse(view['runtime_authority'])
        foreign = obj('I_VALID_SNAPSHOTS.json')['snapshots']['vendor_supported']
        foreign['origin_session'] = 'other-session'
        view = self.api('owner_view')(canonical(foreign), 'I')
        self.assertEqual('other-session', view['reported']['origin_session'])
        self.assertEqual('TEST-SESSION-A', view['reported']['scopes'][0]['origin_session'])
        self.assertEqual('unknown', view['currentness']['state'])

    def test_d_stale_semantics_and_floating_metadata_remain_foreign(self):
        raw = source('D_EX06_Refused_Stale_Record_Bundle.json')
        view = self.api('owner_view')(raw, 'D')
        self.assertEqual('refused', view['reported']['bundleValidation']['admission'])
        self.assertEqual('bundle:stale:100', view['reported']['bundleId'])
        envelope = self.api('owner_envelope')(source('D_EX01_NGX_DX12_Normalized_Bundle.json'), 'D')
        self.assertEqual('unknown', envelope['cell']['identity_extensions']['signal_contract_fingerprint']['state'])
        with self.assertRaises(CoverageError): strict_load(b'{"j_value":1.5}')

    def test_l_active_fixture_never_becomes_current_readiness(self):
        view = self.api('owner_view')(source('L_canonical_ready_synthetic.json'), 'L')
        self.assertEqual('ACTIVE', view['reported']['route']['owner_report'])
        self.assertEqual('SYNTHETIC_FIXTURE', view['reported']['source_mode'])
        self.assertEqual('unknown', view['currentness']['state'])
        self.assertFalse(view['runtime_authority'])

    def test_e_specification_expected_scope_never_becomes_observed_pass(self):
        raw = source('E_replay_result_specification.json')
        view = self.api('owner_view')(raw, 'E')
        self.assertEqual('NOT_READY', view['reported']['execution_status'])
        self.assertEqual('NOT_RUN', view['reported']['layers'][0]['status'])
        envelope = self.api('owner_envelope')(raw, 'E')
        self.assertEqual('unknown', envelope['record']['observed_result'])
        self.assertEqual('unknown', envelope['record']['scope']['scenario_sha256']['state'])

    def test_foreign_schema_and_missing_wire_contract_are_retained_unmapped(self):
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            importer = self.api('import_owner')
            raw = b'{"execution_permission":true,"owner":"I"}'
            result = importer(raw, 'I', store, schema_sha256='f' * 64)
            self.assertEqual('PUBLISHED', result['status'])
            self.assertEqual('UNMAPPED', result['mapping_status'])
            self.assertIn('OWNER_SCHEMA_UNSUPPORTED', result['limitations'])
            self.assertEqual('REPORTED', result['intake_class'])
            for owner in ('A', 'H'):
                result = importer(raw, owner, store)
                self.assertEqual('UNMAPPED', result['mapping_status'])
                self.assertIn('OWNER_WIRE_CONTRACT_UNAVAILABLE', result['limitations'])

    def test_new_owner_version_is_preserved_unmapped_not_guessed(self):
        snapshot = obj('I_VALID_SNAPSHOTS.json')['snapshots']['empty_unknown']
        snapshot['schema_version'] = '99.0.0'
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            result = self.api('import_owner')(canonical(snapshot), 'I', store)
            self.assertEqual('PUBLISHED', result['status'])
            self.assertEqual('UNMAPPED', result['mapping_status'])
            self.assertIn('OWNER_SCHEMA_UNSUPPORTED', result['limitations'])

    def test_forged_g_admission_is_quarantined_and_raw_retained(self):
        candidate = obj('G_candidate_examples.json')['examples'][0]['record']
        candidate['may_admit'] = True
        raw = canonical(candidate)
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            result = self.api('import_owner')(raw, 'G', store)
            self.assertEqual('QUARANTINED', result['status'])
            self.assertEqual(['OWNER_SCHEMA_INVALID'], result['reason_codes'])
            self.assertTrue(result['raw_retained'])
            self.assertEqual(0, store.count('evidence'))

    def test_typed_import_is_idempotent_and_preserves_schema_pin(self):
        raw = source('I_VALID_SNAPSHOTS.json')
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            importer = self.api('import_owner')
            first = importer(raw, 'I', store, selector='/snapshots/vendor_supported')
            retry = importer(raw, 'I', store, selector='/snapshots/vendor_supported')
            self.assertEqual('PUBLISHED', first['status'])
            self.assertEqual('TYPED_HISTORICAL', first['mapping_status'])
            self.assertTrue(retry['idempotent'])
            record = json.loads(store.db.execute('SELECT record_json FROM evidence').fetchone()[0])
            self.assertEqual(digest((SCHEMAS / 'I.json').read_bytes()), record['external_references'][0]['schema_sha256']['value'])
            self.assertEqual('REPORTED', first['intake_class'])

    def test_a_effective_legacy_route_has_no_capability_or_default_inference(self):
        project = self.api('present_route_view')
        self.assertEqual('SyntheticControls', project(1)['input_policy']['value'])
        self.assertEqual('MatchedGameGuidesRequired', project(2)['input_policy']['value'])
        self.assertEqual('NativeTemporal', project(0)['family']['value'])
        for invalid in (None, True, 3, '2'):
            self.assertEqual('unknown', project(invalid)['family']['state'])
        self.assertFalse(project(2)['runtime_authority'])

    def test_exact_original_claim_bytes_migrate_as_original_not_inventory(self):
        raw = source('J03_Current_Evidence_Map.json')
        self.assertEqual('c7555743edb9ada102926f2e5c7801d8d10ab06529f1b2e1109133c95c786ff3', digest(raw))
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            result = migrate_claims(raw, store, publish=True)
            retry = migrate_claims(raw, store, publish=True)
            self.assertEqual('original_claim_map', result['source_kind'])
            self.assertEqual(69, result['record_count'])
            self.assertEqual('COV-CL-024', result['records'][23]['original_id'])
            self.assertEqual('contested_inference', result['records'][23]['original_status'])
            self.assertTrue(all(row['result']['idempotent'] for row in retry['records']))
            self.assertEqual(69, store.count('evidence'))

    def test_malformed_claim_has_typed_refusal(self):
        with self.assertRaises(CoverageError) as caught: migrate_claims(b'{"claims":[null]}')
        self.assertEqual('MIGRATION_IDS_INVALID', caught.exception.code)

    def test_foreign_selector_can_retain_fractional_metadata_without_j_coercion(self):
        document = obj('D_EX01_NGX_DX12_Normalized_Bundle.json')
        document['sourceSnapshot']['value'] = 1.5
        raw = canonical({'historical': document})
        with tempfile.TemporaryDirectory() as tmp, Store(Path(tmp) / 'ledger') as store:
            result = self.api('import_owner')(raw, 'D', store, selector='/historical')
            self.assertEqual('PUBLISHED', result['status'])
            self.assertEqual(1.5, self.api('owner_view')(raw, 'D', selector='/historical')['reported']['sourceSnapshot']['value'])

    def test_evd005_invokes_real_canonical_evaluator_without_issuer(self):
        from tools.nr_coverage.lab_cases import run_lab_case
        with tempfile.TemporaryDirectory() as tmp:
            try: result = run_lab_case('NLAB-EVD-005', Path(tmp) / 'case')
            except CoverageError as exc: self.fail('Missing canonical no-issuer control: ' + str(exc))
            self.assertTrue(result['success'], result['checks'])
            observations = result['owner_observations']['original_coverage']
            self.assertTrue(observations['before']['structural_valid'])
            self.assertTrue(observations['after']['structural_valid'])
            self.assertEqual(129, len(observations['after']['unsatisfied_obligations']))
            self.assertEqual(observations['before']['unsatisfied_obligations'], observations['after']['unsatisfied_obligations'])
            self.assertTrue(observations['after']['unverified_producers'])
            self.assertFalse(observations['after']['human_g2_granted'])

    def test_owner_cli_import_and_readback_preserve_unreviewed_status(self):
        from tools.nr_coverage.__main__ import main
        with tempfile.TemporaryDirectory() as tmp:
            args = ['--root', str(Path(tmp) / 'ledger'), 'owner-import', '--owner', 'I',
                    '--raw', str(HERE / 'I_VALID_SNAPSHOTS.json'), '--selector', '/snapshots/vendor_supported']
            output = io.StringIO()
            try:
                with contextlib.redirect_stdout(output): code = main(args)
            except SystemExit as exc: self.fail('Owner import command unavailable: ' + str(exc))
            result = json.loads(output.getvalue())
            self.assertEqual(0, code)
            self.assertEqual('REPORTED', result['intake_class'])
            self.assertEqual('TYPED_HISTORICAL', result['mapping_status'])
            args[2] = 'owner-view'; output = io.StringIO()
            with contextlib.redirect_stdout(output): code = main(args)
            view = json.loads(output.getvalue())
            self.assertEqual(0, code)
            self.assertEqual('supported', view['reported']['facts'][0]['assertion']['value'])
            self.assertEqual('unknown', view['currentness']['state'])
