import io, json, struct, sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'tools/nr_semantic_worker'))
from protocol import read_message, write_message, validate_request

class ProtocolTests(unittest.TestCase):
    def test_bounded_frame(self):
        with self.assertRaises(ValueError): read_message(io.BytesIO(struct.pack('<I',65537)))
    def test_truncated_frame(self):
        with self.assertRaises(EOFError): read_message(io.BytesIO(b'\x03\0\0\0{}'))
    def test_json_roundtrip(self):
        b=io.BytesIO();write_message(b,{'schema':1});b.seek(0)
        self.assertEqual(read_message(b),{'schema':1})
    def test_nan_rejected(self):
        b=io.BytesIO();raw=b'{"score":NaN}';b.write(struct.pack('<I',len(raw))+raw);b.seek(0)
        with self.assertRaises(ValueError): read_message(b)
    def test_tracking_schema_has_bounded_hold(self):
        r={'schema':3,'nonce':'a'*32,'sequence':1,'epoch':[1]*8,'capture_ns':1,
           'width':4,'height':4,'stride':16,'bytes':64,'maximum_persons':16,'pose_requested':True,'detect_objects':True,'hold_ms':80}
        self.assertEqual(validate_request(r,'a'*32),r)
        for value in (True,-1,2001,80.0):
            with self.assertRaises(ValueError):validate_request(dict(r,hold_ms=value),'a'*32)
    def test_bounds_and_nonce(self):
        r={'schema':2,'nonce':'a'*32,'sequence':1,'epoch':[1]*8,'capture_ns':1,
           'width':4,'height':4,'stride':16,'bytes':64,'maximum_persons':4,'pose_requested':False,'detect_objects':False}
        self.assertEqual(validate_request(r,'a'*32),r)
        for key,val in [('width',True),('height',2049),('stride',15),('bytes',2**30),('nonce','b'*32),('sequence',0),('epoch',[1]*7)]:
            bad=dict(r);bad[key]=val
            with self.assertRaises(ValueError):validate_request(bad,'a'*32)

if __name__=='__main__': unittest.main()
