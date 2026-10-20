import importlib.util,json,pathlib,tempfile,unittest,hashlib,sys
root=pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'tools/depth-anything-v2'))
from PrepareCache import publish
class CachePublication(unittest.TestCase):
 def test_cancellation_preserves_prior_generation(self):
  with tempfile.TemporaryDirectory(dir=root/'builds/depth-anything-v2') as folder:
   folder=pathlib.Path(folder);building=folder/'building';dest=folder/'current';building.mkdir();dest.mkdir();(dest/'sentinel').write_text('prior')
   with self.assertRaises(RuntimeError):publish(building,dest,[],cancelled=True)
   self.assertEqual((dest/'sentinel').read_text(),'prior')
 def test_corruption_never_publishes(self):
  with tempfile.TemporaryDirectory(dir=root/'builds/depth-anything-v2') as folder:
   folder=pathlib.Path(folder);building=folder/'building';dest=folder/'current';building.mkdir();(building/'engine.plan').write_bytes(b'bad')
   (building/'depth-cache.json').write_text(json.dumps({'builder':'neurotic-dav2-local-v1','parity':'synthetic-fp32-reference-passed','files':{'engine.plan':'0'*64}}))
   with self.assertRaises(RuntimeError):publish(building,dest,['engine.plan'])
   self.assertFalse(dest.exists())
 def test_valid_generation_publishes_atomically(self):
  with tempfile.TemporaryDirectory(dir=root/'builds/depth-anything-v2') as folder:
   folder=pathlib.Path(folder);building=folder/'building';dest=folder/'current';building.mkdir();(building/'engine.plan').write_bytes(b'owned')
   sha=hashlib.sha256(b'owned').hexdigest();(building/'depth-cache.json').write_text(json.dumps({'builder':'neurotic-dav2-local-v1','parity':'synthetic-fp32-reference-passed','files':{'engine.plan':sha}}))
   publish(building,dest,['engine.plan']);self.assertFalse(building.exists());self.assertEqual((dest/'engine.plan').read_bytes(),b'owned')
if __name__=='__main__':unittest.main()
