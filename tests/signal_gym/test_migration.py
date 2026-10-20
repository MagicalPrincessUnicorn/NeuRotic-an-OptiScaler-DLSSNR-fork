import tempfile
import unittest
from pathlib import Path
from test_storage import CORPUS
from storage import GymError
from migration import *
from runner import replay_case
LEGACY=CORPUS.parent.parent/'legacy'
class MigrationTests(unittest.TestCase):
    def test_generation_known_zero_not_unknown(self):
        self.assertEqual(legacy_generation(0),{'state':'known','value':'0'});self.assertEqual(legacy_generation('epoch-5')['state'],'not_observed')
        with self.assertRaises(GymError):legacy_generation('18446744073709551616')
    def test_id_and_scalar_preservation(self):
        self.assertEqual(legacy_id('bad / id'),legacy_id('bad / id'))
        self.assertEqual(legacy_scalar({'f32_bits_hex':'80000000','unit':'unitless'})['f32_bits_hex'],'80000000')
        self.assertEqual(legacy_scalar({'f32_bits_hex':'00000000'})['state'],'ambiguous')
    def test_incomplete_legacy_is_invalid_and_never_published(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'legacy.json';p.write_text('{"schema_version":"1.0.0-provisional"}')
            with self.assertRaises(GymError) as e:migrate(p,Path(d)/'output')
            self.assertEqual(e.exception.status,'INVALID_CORPUS');self.assertFalse((Path(d)/'output').exists())

    def test_map1_preserves_observations_with_no_automatic_invocations(self):
        with tempfile.TemporaryDirectory() as d:
            before=read(LEGACY/'GYM-FX-001.json');out=Path(d)/'one'
            receipt=migrate(LEGACY/'GYM-FX-001.json',out)
            c=load_corpus(out/'corpus_manifest.json')[0];f=c['frames'][0]
            self.assertEqual(receipt['status'],'storage_migrated');self.assertFalse(receipt['executable_equivalent'])
            self.assertEqual(read(LEGACY/'GYM-FX-001.json'),before)
            self.assertEqual(f['identity']['device']['logical_id']['state'],'not_observed')
            self.assertEqual(f['identity']['device']['generation'],{'state':'known','value':'1'})
            self.assertEqual(f['identity']['provider_generation']['state'],'not_observed')
            self.assertEqual(f['identity']['native_eval_id']['state'],'not_observed')
            self.assertEqual(f['signals'][1]['scalars']['scale_x']['f32_bits_hex'],'3f800000')
            self.assertEqual(f['source_contract_inputs']['legacy_declarations']['creation_flags']['mv_jittered'],{'state':'known','value':False})
            self.assertTrue(all(e['operation'] is None and e['kind']=='observation' for e in f['events']))
            self.assertEqual([e['observation']['event_type'] for e in f['events']], [e['event_type'] for e in parse(before)['events']])
            self.assertEqual(replay_case(c,{})[0]['execution_status'],'NOT_READY')
            self.assertGreater(len(c['expectations']['transport_checks']),0)
            self.assertEqual(f['resources'][0]['payload']['reason_id'],'GYM.LEGACY_GENERATOR_UNBOUND')
            self.assertEqual(f['provenance']['distribution'],'quarantine')
            two=Path(d)/'two';migrate(LEGACY/'GYM-FX-001.json',two)
            self.assertEqual(read(out/'MIGRATION_REPORT.json'),read(two/'MIGRATION_REPORT.json'))

    def test_all_recovered_fixtures_and_directory_are_storage_migratable(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'all';migrate(LEGACY,out)
            self.assertEqual(len(load_corpus(out/'corpus_manifest.json')),20)

    def test_scalar_rounding_disagreement_and_signed_zero(self):
        for decimal,bits,want in [('1','3f800000','known'),('1','40000000','ambiguous'),('-0','00000000','ambiguous'),('-0','80000000','known'),('1.000000059604644775390625','3f800000','known'),('1.000000059604644775390626','3f800001','known')]:
            with self.subTest(decimal=decimal,bits=bits):
                self.assertEqual(legacy_scalar({'state':'known','decimal':decimal,'f32_bits_hex':bits,'unit':'unitless'})['state'],want)
        self.assertEqual(legacy_scalar({'state':'synthetic_corruption','corruption_token':'nan','unit':'unitless'})['corruption_token'],'nan')

    def test_inline_bits_pack_little_endian_and_corrupt_external_rejected(self):
        value=parse(read(LEGACY/'GYM-FX-001.json'))
        value['resources'][0].update(format='R32_FLOAT',width=2,height=1)
        value['resources'][0]['payload']={'kind':'inline_field','element_type':'f32','components':1,'width':2,'height':1,'encoding':'row_major_hex_bits','values':['3f800000','80000000'],'license_id':'CC0-1.0','media_type':'application/octet-stream'}
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'fixture.json';p.write_bytes(canon(value));out=Path(d)/'valid';migrate(p,out)
            c=load_corpus(out/'corpus_manifest.json')[0];payload=c['frames'][0]['resources'][0]['payload']
            self.assertEqual(payload['raw_sha256'],sha(bytes.fromhex('0000803f00000080')))
            self.assertEqual(b''.join(c['verified_blobs'][x['sha256']] for x in payload['chunks']),bytes.fromhex('0000803f00000080'))
            value['resources'][0]['payload']={'kind':'external_blob','sha256':'0'*64,'relative_path':'blob.bin','license_id':'CC0-1.0','media_type':'application/octet-stream'}
            (Path(d)/'blob.bin').write_bytes(b'bad');p.write_bytes(canon(value))
            with self.assertRaisesRegex(GymError,'HASH_MISMATCH'):migrate(p,Path(d)/'bad')

    def test_legacy_freeform_decimal_event_retained_without_executable_number(self):
        value=parse(read(LEGACY/'GYM-FX-001.json'));value['events'][0]['details']={'measurement':0.125}
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'fixture.json';raw=json.dumps(value).encode();p.write_bytes(raw);out=Path(d)/'out'
            report=migrate(p,out);c=load_corpus(out/'corpus_manifest.json')[0]
            self.assertEqual(read(out/report['sources'][0]['source_ref']),raw)
            self.assertEqual(c['frames'][0]['events'][0]['observation']['legacy_source_pointer'],'/events/0')
if __name__=='__main__':unittest.main()
