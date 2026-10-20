"""Inert real-import consumer controls, never actual build/tool evidence.

Only source copies and tiny fixtures under the runner's owned temporary tree
are read. No compiler, fake tool file, or module observer is executed. capture's
hardcoded OBSERVED_BUILD label stays in memory for assertions; no fixture
build receipt or eligible artifact is written or offered to another consumer.
"""
from pathlib import Path
import copy, hashlib, json, re, sys, tempfile, unittest
sys.path.insert(0,str(Path(__file__).parents[1]))
import BuildEvidence as e
import ProducerCapture as c

def pin(p,kind='source'):
    b=p.read_bytes()
    return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),kind=kind)

class ConsumerControls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='inert-consumer-',dir=Path(__file__).parent)
        self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.output=self.root/'builds/fixture';self.tracking=self.output/'obj/Fixture.tlog';self.tracking.mkdir(parents=True)
        self.source=self.root/'source.cpp';self.source.write_bytes(b'INERT_SOURCE')
        self.header=self.root/'header.h';self.header.write_bytes(b'INERT_HEADER')
        self.library=self.root/'fixture.lib';self.library.write_bytes(b'INERT_LIBRARY')
        self.obj=self.output/'obj/source.obj';self.obj.write_bytes(b'INERT_OBJECT_NOT_EXECUTABLE')
        self.binary=self.output/'bin/NeuRotic.exe';self.binary.parent.mkdir();self.binary.write_bytes(b'INERT_OUTPUT_NOT_EXECUTABLE')
        self.pins=[pin(p) for p in (self.source,self.header,self.library)]
        self.helper=Path(c.__file__).resolve();self.pins.append(pin(self.helper,'recipe'))
        for name in ('cl.exe','c1xx.dll','c2.dll','link.exe'):
            p=self.root/name;p.write_bytes(b'INERT_TOOL_NEVER_EXECUTED');self.pins.append(pin(p,'tool'))
        self.manifest=self.root/'manifest.json';self.evaluation=self.root/'evaluation.json';self.log=self.root/'build.log'
        self.evaluation.write_text(json.dumps(dict(inputs=[pin(self.source)])));self.log.write_text('FIXTURE_POLICY_ONLY: no build ran\n')
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj])
        self.logs('link',str(self.obj),[self.library],[self.binary])
    def logs(self,stem,root,reads,writes,write_root=None):
        for family,text in [('read','^'+root+'\n'+'\n'.join(map(str,reads))),
                            ('write','^'+(write_root or root)+'\n'+'\n'.join(map(str,writes))),
                            ('command','^'+root+'\n/FIXTURE_POLICY_ONLY')]:
            (self.tracking/(stem+'.'+family+'.1.tlog')).write_text(text+'\n',encoding='utf-16')
    def capture(self):
        self.manifest.write_text(json.dumps(dict(target='app',native_only=True,files=self.pins)))
        return e.capture(self.root,self.output,self.manifest,self.log,self.evaluation)
    def receipt_fixture(self):
        evidence=self.capture()
        return dict(schema_version=2,target='app',exit_code=0,source_unchanged=True,repository=str(self.root),
                    source_manifest_path=str(self.manifest),input_pins=self.pins,evidence=evidence,
                    files=[dict(path=str(self.binary.relative_to(self.root)),sha256=pin(self.binary)['sha256'],bytes=self.binary.stat().st_size)])
    def test_imports_same_shared_parser(self):
        self.assertTrue(hasattr(c,'tlog_records'),'Shared parser export missing')
        self.assertTrue(hasattr(e,'tlog_records'),'Common parser import missing')
        self.assertIs(e.tlog_records,c.tlog_records)
    def test_valid_source_root_is_read_provenance_not_compile_output(self):
        result=self.capture();compile=next(p for p in result['producers'] if p['kind']=='CL')
        self.assertIn(str(self.source),[r['path'] for r in compile['inputs']])
        self.assertEqual([r['path'] for r in compile['outputs']],[str(self.obj)])
        self.assertEqual([r['path'] for r in result['selected_outputs']],[str(self.binary)])
    def test_link_object_root_is_input_not_linker_output(self):
        link=next(p for p in self.capture()['producers'] if p['kind']=='link')
        self.assertIn(str(self.obj),[r['path'] for r in link['inputs']])
        self.assertEqual([r['path'] for r in link['outputs']],[str(self.binary)])
    def test_shared_helper_is_required_consumed_freeze_input(self):
        result=self.capture()
        self.assertIn(str(self.helper),[r['path'] for r in result['required_inputs']])
    def test_missing_shared_helper_pin_refused(self):
        self.pins=[r for r in self.pins if Path(r['path'])!=self.helper]
        with self.assertRaisesRegex(ValueError,'Shared native tracking parser absent from freeze'):self.capture()
    def test_changed_shared_helper_pin_refused(self):
        next(r for r in self.pins if Path(r['path'])==self.helper)['sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'Evidence input changed'):self.capture()
    def test_actual_unowned_write_value_refused(self):
        outside=self.root/'outside.obj';outside.write_bytes(b'SENTINEL')
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj,outside])
        with self.assertRaisesRegex(ValueError,'Unowned native write: '+re.escape(str(outside))):self.capture()
        self.assertEqual(outside.read_bytes(),b'SENTINEL')
    def test_absent_unowned_write_value_refused_before_file_filter(self):
        outside=self.root/'not-created.obj'
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj,outside])
        with self.assertRaisesRegex(ValueError,'Unowned native write: '+re.escape(str(outside))):self.capture()
        self.assertFalse(outside.exists())
    def test_mismatched_root_refused(self):
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj],str(self.header))
        with self.assertRaisesRegex(ValueError,'Native read/write tracking roots differ'):self.capture()
    def test_headerless_write_payload_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text(str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Tracking payload precedes record root'):self.capture()
    def test_duplicate_root_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text(('^'+str(self.source)+'\n'+str(self.obj)+'\n')*2,encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Duplicate tracking record root'):self.capture()
    def test_empty_compound_key_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text('^'+str(self.source)+'|\n'+str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Malformed tracking record root'):self.capture()
    def test_relative_root_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text('^relative.cpp\n'+str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaises(ValueError):self.capture()
    def test_compound_payload_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text('^'+str(self.source)+'\n'+str(self.obj)+'|'+str(self.binary)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Unmodeled tracking path'):self.capture()
    def test_unfrozen_record_root_source_refused(self):
        self.logs('CL',str(self.source),[self.header],[self.obj])
        self.evaluation.write_text(json.dumps(dict(inputs=[pin(self.header)])))
        self.pins=[r for r in self.pins if Path(r['path'])!=self.source]
        with self.assertRaisesRegex(ValueError,'Consumed input absent from freeze: '+re.escape(str(self.source))):self.capture()
    def test_unfrozen_read_payload_refused(self):
        self.pins=[r for r in self.pins if Path(r['path'])!=self.header]
        with self.assertRaisesRegex(ValueError,'Consumed input absent from freeze: '+re.escape(str(self.header))):self.capture()
    def test_changed_frozen_read_refused(self):
        self.header.write_bytes(b'CHANGED')
        with self.assertRaisesRegex(ValueError,'Consumed input changed: '+re.escape(str(self.header))):self.capture()
    def test_missing_tool_pin_refused(self):
        self.pins=[r for r in self.pins if Path(r['path']).name!='c2.dll']
        with self.assertRaisesRegex(ValueError,'Actual tool pin missing/ambiguous: c2.dll'):self.capture()
    def test_generated_root_without_producer_refused(self):
        orphan=self.output/'obj/orphan.obj';orphan.write_bytes(b'UNPROVEN')
        self.logs('link',str(orphan),[self.library],[self.binary])
        with self.assertRaisesRegex(ValueError,'Unproven generated inputs or cyclic producer graph'):self.capture()
    def test_selected_output_without_write_value_refused(self):
        other=self.output/'bin/other.exe';other.write_bytes(b'INERT_OTHER')
        self.logs('link',str(self.obj),[self.library],[other])
        with self.assertRaisesRegex(ValueError,'Selected target output not produced: '+re.escape(str(self.binary))):self.capture()
    def test_no_existing_owned_write_value_refused(self):
        self.logs('CL',str(self.source),[self.source,self.header],[self.output/'obj/absent.obj'])
        with self.assertRaisesRegex(ValueError,'Producer writes missing'):self.capture()
    def test_missing_command_trace_refused(self):
        (self.tracking/'CL.command.1.tlog').unlink()
        with self.assertRaisesRegex(ValueError,'Missing command/write tracking'):self.capture()
    def test_unknown_producer_family_refused(self):
        self.logs('unknown',str(self.source),[self.source],[self.obj])
        with self.assertRaisesRegex(ValueError,'Unmodeled producer tracking: unknown'):self.capture()
    def test_out_of_order_and_compound_generated_root_retains_edges(self):
        self.logs('link',str(self.obj)+'|'+str(self.library),[self.library],[self.binary])
        for stem,folder in [('link','ALink'),('CL','ZCompile')]:
            target=self.output/'obj'/folder;target.mkdir()
            for p in self.tracking.glob(stem+'.*.tlog'):p.rename(target/p.name)
        result=self.capture()
        self.assertIn(str(self.obj),result['generated_inputs'])
        self.assertEqual([p['kind'] for p in result['producers']],['CL','link'])
    def test_retained_receipt_replay_accepts_equal_inert_fixture(self):
        self.assertIsNone(e.validate_receipt(self.receipt_fixture()))
    def test_retained_receipt_replay_refuses_changed_trace(self):
        receipt=self.receipt_fixture()
        (self.tracking/'link.command.1.tlog').write_text('changed',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Evidence input changed'):e.validate_receipt(receipt)
    def test_retained_receipt_replay_refuses_changed_producer_claim(self):
        receipt=copy.deepcopy(self.receipt_fixture());receipt['evidence']['producers'][0]['command']='INVENTED_COMMAND'
        with self.assertRaisesRegex(ValueError,'Retained actual producer trace differs'):e.validate_receipt(receipt)


    def test_command_record_contract_round1(self):
        cases=[('unrelated','^'+str(self.header)+'\n/c fixture\n','Command tracking roots differ'),
               ('extra','^'+str(self.source)+'\n/c fixture\n^'+str(self.header)+'\n/c fixture\n','Command tracking roots differ'),
               ('duplicate',('^'+str(self.source)+'\n/c fixture\n')*2,'Duplicate tracking record root'),
               ('headerless','/c fixture\n','Tracking payload precedes record root'),
               ('empty','^'+str(self.source)+'\n','Empty native tracking record'),
               ('relative','^relative.cpp\n/c fixture\n','Unmodeled tracking record root'),
               ('malformed','^'+str(self.source)+'|\n/c fixture\n','Malformed tracking record root'),
               ('multiline','^'+str(self.source)+'\n/c fixture\n/extra unknown line\n','Command tracking record needs one payload')]
        for label,text,error in cases:
            with self.subTest(command_form=label):
                (self.tracking/'CL.command.1.tlog').write_text(text,encoding='utf-16')
                with self.assertRaisesRegex(ValueError,error):self.capture()
    def test_missing_command_record_round1(self):
        other=self.output/'second.obj';other.write_bytes(b'INERT_SECOND_OBJECT')
        for family,value in [('read',self.header),('write',other)]:
            p=self.tracking/('CL.'+family+'.1.tlog')
            p.write_text(p.read_text(encoding='utf-16')+'^'+str(self.header)+'\n'+str(value)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Command tracking roots differ'):self.capture()
    def test_opaque_command_spelling_and_pipe_round1(self):
        payload='  /D "VALUE=A|B" /Fo"C:/space with blanks/../obj/"  '
        p=self.tracking/'CL.command.1.tlog';p.write_text('^'+str(self.source)+'\n'+payload+'\n',encoding='utf-16')
        result=self.capture();producer=next(r for r in result['producers'] if r['kind'].lower()=='cl')
        self.assertEqual(producer['command'],p.read_text(encoding='utf-16'))
    def test_parent_escape_values_round1(self):
        import re
        for present,suffix in [(True,'.obj'),(False,'.txt')]:
            with self.subTest(existing=present,suffix=suffix):
                foreign=self.output.parent/('foreign'+suffix)
                if present:foreign.write_bytes(b'FOREIGN_SENTINEL')
                raw=self.output/'..'/foreign.name
                self.logs('CL',str(self.source),[self.source,self.header],[self.obj,raw])
                with self.assertRaisesRegex(ValueError,'Unowned native write: '+re.escape(str(raw))):self.capture()
                if present:self.assertEqual(foreign.read_bytes(),b'FOREIGN_SENTINEL')
                else:self.assertFalse(foreign.exists())
    def test_normalized_owned_write_preserves_raw_spelling_round1(self):
        (self.output/'normalize').mkdir()
        raw=self.output/'normalize'/'..'/self.obj.relative_to(self.output)
        self.logs('CL',str(self.source),[self.source,self.header],[raw])
        producer=next(r for r in self.capture()['producers'] if r['kind'].lower()=='cl')
        self.assertEqual([r['path'] for r in producer['outputs']],[str(raw.absolute())])

if __name__=='__main__':unittest.main(verbosity=2)
