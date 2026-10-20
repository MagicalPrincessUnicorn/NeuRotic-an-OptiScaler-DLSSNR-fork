from pathlib import Path
import hashlib,json,sys,tempfile,unittest,copy
sys.path.insert(0,str(Path(__file__).parents[1]))
import BuildEvidence as evidence
class ModuleEvents(unittest.TestCase):
 def test_actual_event_graph_and_intended_omissions(self):
  verify=getattr(evidence,'validate_module_events',None)
  self.assertTrue(callable(verify),'Dedicated event graph adapter missing')
  with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as scratch:
   root=Path(scratch);tool=root/'tool.exe';module=root/'support.dll';tool.write_bytes(b'INERT_POLICY_FIXTURE');module.write_bytes(b'INERT_MODULE');pins=[evidence.row(tool),evidence.row(module)];events=root/'events.jsonl'
   rows=[dict(kind='begin',version=1,target=str(tool),timeout_ms=90000),dict(kind='create',pid=1,tid=1,root=True),dict(kind='module',pid=1,tid=1,base=1,path=str(tool),path_error=0),dict(kind='create',pid=2,tid=2,root=False),dict(kind='module',pid=2,tid=2,base=2,path=str(module),path_error=0),dict(kind='exit',pid=2,tid=2,exit_code=0),dict(kind='exit',pid=1,tid=1,exit_code=0),dict(kind='end',complete=True,exit_code=0)]
   for i,r in enumerate(rows):r['seq']=i
   def write(values):events.write_text(''.join(json.dumps(r)+'\n' for r in values))
   write(rows);result=verify(events,pins,str(tool));self.assertEqual(len(result['modules']),2);self.assertEqual(result['process_count'],2)
   with self.assertRaisesRegex(ValueError,'Unfrozen actual module'):verify(events,pins[:1],str(tool))
   with self.assertRaisesRegex(ValueError,'Root tool differs'):verify(events,pins,str(module))
   for label,change in [('truncated',lambda r:r.pop()),('gap',lambda r:r[3].update(seq=99)),('unresolved',lambda r:r[4].update(path_error=6)),('timeout',lambda r:r[-1].update(complete=False)),('root-failure',lambda r:r[-2].update(exit_code=1)),('missing-process-exit',lambda r:r[5].update(kind='unload',base=2))]:
    values=copy.deepcopy(rows);change(values);write(values)
    with self.subTest(label=label),self.assertRaises(ValueError):verify(events,pins,str(tool))
   write(rows)
   # File changes after an otherwise complete event stream invalidate the pin.
   module.write_bytes(b'CHANGED')
   with self.assertRaisesRegex(ValueError,'Evidence input changed'):verify(events,pins,str(tool))
if __name__=='__main__':unittest.main()
