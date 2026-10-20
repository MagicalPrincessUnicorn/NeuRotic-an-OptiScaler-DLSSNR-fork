import hashlib
import importlib.util
import json
import re
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('canonical_package', ROOT/'tools/nr/provider/canonical_package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class CanonicalPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root/'pack.zip'

    def fixture(self, entries=None):
        entries = entries or [('pack/a.dll', b'fixture-a'), ('pack/b.dll', b'fixture-b')]
        with zipfile.ZipFile(self.archive, 'w') as archive:
            for name, data in entries:
                archive.writestr(name, data)
        return {'archive': {'filename': 'pack.zip', 'bytes': self.archive.stat().st_size,
                            'sha256': package.sha256(self.archive)},
                'files': [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                          for name, data in entries]}

    def test_exact_archive_and_each_member(self):
        result = package.verify_archive(self.archive, self.fixture())
        self.assertEqual(result['member_count'], 2)
        self.assertEqual(result['execution'], 'NOT_RUN')

    def test_changed_archive_rejected(self):
        manifest = self.fixture()
        self.archive.write_bytes(self.archive.read_bytes()+b'x')
        with self.assertRaisesRegex(ValueError, 'ARCHIVE_IDENTITY'):
            package.verify_archive(self.archive, manifest)

    def test_member_hash_rejected_even_with_matching_archive(self):
        manifest = self.fixture()
        manifest['files'][0]['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'MEMBER_IDENTITY'):
            package.verify_archive(self.archive, manifest)

    def test_extra_members_rejected(self):
        manifest = self.fixture()
        manifest['files'].pop()
        with self.assertRaisesRegex(ValueError, 'MEMBER_SET'):
            package.verify_archive(self.archive, manifest)

    def test_duplicate_member_rejected(self):
        manifest = self.fixture([('pack/a.dll', b'a'), ('pack/a.dll', b'b')])
        with self.assertRaisesRegex(ValueError, 'DUPLICATE'):
            package.verify_archive(self.archive, manifest)

    def test_traversal_and_case_collisions_rejected(self):
        for entries in [[('pack/../a.dll', b'a')], [('pack/A.dll', b'a'), ('pack/a.dll', b'b')],
                        [('C:/a.dll', b'a')], [('pack/a.dll:stream', b'a')]]:
            with self.subTest(entries=entries):
                with self.assertRaises(ValueError):
                    package.verify_archive(self.archive, self.fixture(entries))

    def test_prepare_fresh_only_and_verify_tamper(self):
        manifest = self.fixture()
        destination = self.root/'ready'
        receipt = package.prepare(self.archive, destination, manifest)
        self.assertEqual((destination/'a.dll').read_bytes(), b'fixture-a')
        self.assertEqual(receipt['execution'], 'NOT_RUN')
        self.assertTrue((destination/'pack.zip').exists())
        with self.assertRaisesRegex(ValueError, 'DESTINATION_EXISTS'):
            package.prepare(self.archive, destination, manifest)
        (destination/'a.dll').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'MEMBER_IDENTITY'):
            package.verify_directory(destination, manifest)

    def test_pinned_manifest_and_runtime_constants_agree(self):
        manifest = package.load_manifest()
        self.assertEqual(len(manifest['files']), 14)
        header = (ROOT/'OptiScaler/dlssnr/CanonicalProviderPolicy.h').read_text()
        tuples = re.findall(r'\{L"([^"]+)", (\d+)ull, "([a-f0-9]{64})"\}', header)
        expected = {(manifest['archive']['filename'], str(manifest['archive']['bytes']), manifest['archive']['sha256'])}
        expected |= {(Path(e['path']).name, str(e['bytes']), e['sha256']) for e in manifest['files']}
        self.assertEqual(set(tuples), expected)
        self.assertEqual(len(tuples), len(expected))

    def test_prepare_direct_keeps_nr_and_notices_without_archive_requirement(self):
        manifest = self.fixture([('pack/nvngx_dlssnr.dll', b'nr'),
                                 ('pack/nvngx_dlss.dll', b'sr'),
                                 ('pack/sl.common.dll', b'streamline'),
                                 ('pack/nvngx_dlss.license.txt', b'notice')])
        destination = self.root/'direct'
        receipt = package.prepare_direct(self.archive, destination, manifest)
        self.assertEqual(receipt['verification_scope'], 'DIRECT_NR')
        self.assertFalse(receipt['package_verified'])
        self.assertTrue(receipt['selection_verified'])
        self.assertEqual(receipt['execution'], 'NOT_RUN')
        self.assertEqual((destination/'nvngx_dlssnr.dll').read_bytes(), b'nr')
        self.assertEqual(receipt['source_archive_member_count'], 4)
        self.assertEqual(receipt['member_count'], 2)
        self.assertEqual((destination/'nvngx_dlss.license.txt').read_bytes(), b'notice')
        self.assertFalse((destination/'sl.common.dll').exists())
        self.assertFalse((destination/'pack.zip').exists())
        self.assertFalse((destination/'nvngx_dlss.dll').exists())
        with self.assertRaisesRegex(ValueError, 'DESTINATION_EXISTS'):
            package.prepare_direct(self.archive, destination, manifest)

    def test_direct_still_requires_verified_source_archive_and_nr_member(self):
        manifest = self.fixture()
        with self.assertRaisesRegex(ValueError, 'NR_MEMBER_MISSING'):
            package.prepare_direct(self.archive, self.root/'missing', manifest)
        manifest = self.fixture([('pack/nvngx_dlssnr.dll', b'nr')])
        self.archive.write_bytes(self.archive.read_bytes()+b'tamper')
        with self.assertRaisesRegex(ValueError, 'ARCHIVE_IDENTITY'):
            package.prepare_direct(self.archive, self.root/'bad', manifest)

if __name__ == '__main__':
    unittest.main(verbosity=2)
