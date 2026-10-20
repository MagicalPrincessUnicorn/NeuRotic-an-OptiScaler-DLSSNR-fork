"""Production offline boundary tests. All inputs are synthetic or packet design data."""
import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools.nr_coverage.schema import validate, strict_load, CoverageError
from tools.nr_coverage.identity import canonical, digest, condition_key, compatibility_key, refresh_cell, evidence_key
from tools.nr_coverage.store import Store
from tools.nr_coverage.import_pipeline import import_normalized
from tools.nr_coverage.projection import match, project

FIXTURES = Path(__file__).parent / 'fixtures'
def fixture(name):
    return json.loads((FIXTURES / name).read_text())

class LedgerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.store = Store(Path(self.temp.name) / 'ledger')
        self.addCleanup(self.store.close)
        self.envelope = fixture('intake_envelope.json')
        self.raw = (FIXTURES / 'design_report.txt').read_bytes()

    def import_one(self, envelope=None, **kwargs):
        return import_normalized(canonical(envelope or self.envelope), self.raw, self.store, **kwargs)

    def test_retained_v1_vectors(self):
        for vector in fixture('legacy_key_vectors_v1.json')['vectors']:
            preimage = json.loads(vector['canonical_ascii_preimage'])
            self.assertEqual(vector['expected_key'], condition_key(preimage))

    def test_retained_v2_vectors(self):
        vectors = fixture('V2_KEY_VECTORS.json')
        for vector in vectors['vectors']:
            self.assertEqual(vector['expected_composite_key'], compatibility_key(vector['cell']))
            target = vector['cell'] if vector['compare_to_self'] else vectors['vectors'][0]['cell']
            self.assertEqual(vector['expected_match'], match(vector['cell'], target, vectors['scope_definition'])[0])

    def test_strict_json_and_bool_integer(self):
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'\xff', b'[' * 34 + b']' * 34):
            with self.assertRaises(CoverageError): strict_load(raw)
        cell = self.envelope['cell']
        cell['condition']['conditions']['sample_count']['value'] = False
        with self.assertRaises(CoverageError): validate(cell)

    def test_import_reopen_and_idempotence(self):
        first = self.import_one()
        second = self.import_one()
        self.assertEqual('PUBLISHED', first['status'])
        self.assertEqual(first['evidence_id'], second['evidence_id'])
        self.assertTrue(second['idempotent'])
        self.assertEqual(1, self.store.count('evidence'))
        with Store(self.store.root) as reopened:
            query = fixture('query.json')
            result = project(query, reopened)
            self.assertFalse(result['runtime_authority'])
            self.assertNotEqual('SUPPORTED', result['axes']['output_delivery']['status'])

    def test_transaction_rollback(self):
        def fail(): raise RuntimeError('forced-before-commit')
        result = self.import_one(before_commit=fail)
        self.assertEqual('IMPORT_FAILED', result['status'])
        self.assertEqual(0, self.store.count('evidence'))
        self.assertEqual(0, self.store.count('cells'))
        self.assertEqual('PUBLISHED', self.import_one()['status'])

    def test_raw_mismatch_quarantines_without_game_failure(self):
        result = import_normalized(canonical(self.envelope), b'wrong bytes', self.store)
        self.assertEqual('QUARANTINED', result['status'])
        self.assertIn('RAW_HASH_MISMATCH', result['reason_codes'])
        self.assertEqual(0, self.store.count('evidence'))

    def test_description_variants_and_changed_build(self):
        self.import_one()
        changed = copy.deepcopy(self.envelope)
        changed['cell']['condition']['unresolved'].append('Additional source provenance')
        changed['record']['limitations'].append('New interpretation')
        changed['record']['evidence_id'] = evidence_key(changed['record'])
        self.assertEqual('PUBLISHED', self.import_one(changed)['status'])
        self.assertEqual(2, self.store.count('cells'))
        target = copy.deepcopy(changed['cell'])
        target['cohort']['neurotic_module_sha256']['value'] = 'f' * 64
        target = refresh_cell(target)
        self.assertEqual('DIFFERENT', match(changed['cell'], target, changed['record']['scope'])[0])

if __name__ == '__main__': unittest.main()
