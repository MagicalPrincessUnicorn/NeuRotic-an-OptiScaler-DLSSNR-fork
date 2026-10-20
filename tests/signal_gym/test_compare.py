import unittest
from test_storage import CORPUS
from storage import GymError
from compare import compare

def scalar(bits, unit='pixels'):
    return {'state':'known','f32_bits_hex':bits,'unit':unit}

class ComparatorTests(unittest.TestCase):
    def run_check(self, value, expected, absolute='3f000000', relative='00000000'):
        actual={'value':value}
        check={'check_id':'numeric','actual_pointer':'/value','required':True,
               'comparison':'abs_rel','expected':expected,'event_id':{'state':'not_observed'},
               'abs_tolerance':scalar(absolute),'rel_tolerance':scalar(relative,'unitless')}
        result=compare(actual,{'transport_checks':[check],'qualifications':[],'decisions':[]})
        return result,actual

    def test_absolute_boundary_and_outside(self):
        self.assertEqual(self.run_check(scalar('3fc00000'),scalar('3f800000'))[0]['required_failed'],0)
        self.assertEqual(self.run_check(scalar('3fc00001'),scalar('3f800000'))[0]['required_failed'],1)

    def test_relative_boundary_and_signed_zero(self):
        self.assertEqual(self.run_check(scalar('41100000'),scalar('41000000'),'00000000','3e000000')[0]['required_failed'],0)
        self.assertEqual(self.run_check(scalar('80000000'),scalar('00000000'),'00000000')[0]['required_failed'],0)

    def test_nonfinite_unknown_and_unit_mismatch_never_match(self):
        for value in [scalar('7fc00000'),scalar('7f800000'),scalar('3f800000','seconds'),{'state':'unknown'},1]:
            with self.subTest(value=value):
                self.assertEqual(self.run_check(value,scalar('3f800000'))[0]['required_failed'],1)

    def test_bad_tolerance_is_invalid_expectation(self):
        for bits in ['bf800000','7f800000','7fc00000']:
            with self.subTest(bits=bits),self.assertRaises(GymError):
                self.run_check(scalar('3f800000'),scalar('3f800000'),bits)

if __name__=='__main__':unittest.main()
