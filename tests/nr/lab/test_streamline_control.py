"""Mutation checks for the independent control evidence reader; no provider loads."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('nr_control', ROOT/'tools/nr/lab/streamline/control/control.py')
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


class EvidenceTests(unittest.TestCase):
    def init_fixture(self):
        item=next(r for r in control.canonical_manifest()['files'] if Path(r['path']).name=='sl.interposer.dll')
        module={'mapped_path':r'C:\Canonical\sl.interposer.dll','backing_bytes':item['bytes'],'backing_sha256':item['sha256'],'file_version':'2.13.0.0'}
        events=[self.row('module_identity',{'phase':phase,'errors':[],'modules':[copy.deepcopy(module)],'notifications_complete':True}) for phase in ('before_load','after_load')]
        events += [self.row('api_enter',{'name':'slInit'}),self.row('api_return',{'name':'slInit','result':0}),
            self.row('init_boundary',{'device_attachment_attempted':False,'evaluation_attempted':False}),
            self.row('module_identity',{'phase':'after_init','errors':[],'modules':[module],'notifications_complete':True}),
            self.row('terminal',{'mode':'init','status':'OBSERVED','provider_evaluated':False,'api_success':False,
                'gpu_completed':False,'output_attribution':False,'model_correctness':'UNTESTED','control_pass':False})]
        for i,r in enumerate(events):r['seq']=i
        return events

    def init_assess(self,events):
        return control.analyze_events(events,self.supervisor,self.capture,self.nonce,'init',r'C:\Canonical')

    def test_init_discovery_has_no_device_or_evaluation(self):
        events=self.init_fixture()
        self.assertEqual(self.init_assess(events)['verdict'],'INITIALIZATION_DISCOVERY_OBSERVED')
        for name in ('slSetD3DDevice','slEvaluateFeature'):
            mutated=copy.deepcopy(events);mutated[2]['data']['name']=name;mutated[3]['data']['name']=name
            self.assertEqual(self.init_assess(mutated)['verdict'],'EVIDENCE_INVALID')

    def test_init_earlier_identity_failure_cannot_be_observed(self):
        events=self.init_fixture();events[1]['data']['errors']=[{'reason':'LOADED_RUNTIME_PATH_MISMATCH'}]
        self.assertEqual(self.init_assess(events)['verdict'],'EVIDENCE_INVALID')

    def test_init_checkpoint_cannot_precede_init(self):
        events=self.init_fixture();events.insert(2,events.pop(5))
        for i,r in enumerate(events):r['seq']=i
        self.assertEqual(self.init_assess(events)['verdict'],'EVIDENCE_INVALID')

    def test_init_readback_evidence_refused(self):
        for name in ('readback_observed','constants','options','device','submission_enter'):
            events=self.init_fixture();events.insert(-1,self.row(name,{}))
            for i,r in enumerate(events):r['seq']=i
            self.assertEqual(self.init_assess(events)['verdict'],'EVIDENCE_INVALID')

    def test_init_failed_identity_retains_observation(self):
        events=[self.row('api_enter',{'name':'slInit'}),self.row('api_return',{'name':'slInit','result':0},1),
            self.row('module_identity',{'phase':'after_init','errors':[{'reason':'LOADED_RUNTIME_PATH_MISMATCH'}]},2),
            self.row('terminal',{'mode':'init','status':'FAILED','provider_evaluated':False,'api_success':False,
                'gpu_completed':False,'output_attribution':False,'model_correctness':'UNTESTED','control_pass':False},3)]
        self.supervisor['child_exit_code']=3
        result=control.analyze_events(events,self.supervisor,self.capture,self.nonce,'init')
        self.assertEqual(result['verdict'],'CONTROL_LANE_FAILED')
        self.assertFalse(result['provider_evaluated'])

    def setUp(self):
        self.nonce = 'a' * 32
        self.supervisor = {'process_status':'exited','child_drained':True,'timed_out':False,
                           'unhandled_exception':None,'child_pid':41,'child_exit_code':0}
        self.capture = {'capture_complete':True,'stdout':{'truncated':False},'stderr':{'truncated':False}}
        self.events = [self.row('terminal', {'mode':'cpu','status':'OBSERVED', 'provider_evaluated':False,
            'api_success':False,'gpu_completed':False,'output_attribution':False,'model_correctness':'UNTESTED',
            'control_pass':False,'tests_passed':21,'tests_failed':0})]

    def row(self, name, data, seq=0):
        return {'schema_version':1,'seq':seq,'pid':41,'created_filetime':1234,'nonce':self.nonce,'event':name,'data':data}

    def assess(self):
        return control.analyze_events(self.events,self.supervisor,self.capture,self.nonce,'cpu')

    def invalid(self):
        self.assertEqual(self.assess()['verdict'],'EVIDENCE_INVALID')

    def test_cpu_only_claim(self):
        result=self.assess()
        self.assertEqual(result['verdict'],'CPU_CONTRACT_CHECKS_PASSED')
        self.assertFalse(result['control_pass'])
        self.assertFalse(result['provider_admission'])

    def test_nonce_mutation(self):
        self.events[0]['nonce']='b'*32; self.invalid()

    def test_pid_mutation(self):
        self.events[0]['pid']=42; self.invalid()

    def test_sequence_mutation(self):
        self.events[0]['seq']=1; self.invalid()

    def test_boolean_sequence_refused(self):
        self.events[0]['seq']=False; self.invalid()

    def test_missing_terminal(self):
        self.events=[]; self.invalid()

    def test_null_trace_row(self):
        self.events=[None]; self.invalid()

    def test_array_trace_row(self):
        self.events=[[]]; self.invalid()

    def test_duplicate_terminal(self):
        self.events.append(copy.deepcopy(self.events[0])); self.events[1]['seq']=1; self.invalid()

    def test_capture_truncation(self):
        self.capture['stderr']['truncated']=True; self.invalid()

    def test_timeout_not_pass(self):
        self.supervisor['timed_out']=True; self.invalid()

    def test_undrained_not_pass(self):
        self.supervisor['child_drained']=False; self.invalid()

    def test_exception_not_pass(self):
        self.supervisor['unhandled_exception']={}; self.invalid()

    def test_wrong_mode(self):
        self.events[0]['data']['mode']='physical'; self.invalid()

    def test_false_provider_claim(self):
        self.events[0]['data']['provider_evaluated']=True; self.invalid()

    def test_false_physical_pass(self):
        self.events[0]['data']['control_pass']=True; self.invalid()

    def test_zero_checks_not_pass(self):
        self.events[0]['data']['tests_passed']=0; self.invalid()

    def test_failed_checks_not_pass(self):
        self.events[0]['data']['tests_failed']=1; self.invalid()

    def test_child_nonzero_not_pass(self):
        self.supervisor['child_exit_code']=3; self.invalid()

    def test_unknown_creation_identity(self):
        self.events[0]['created_filetime']=0; self.invalid()

    def test_canonical_mismatch_scoped_refusal(self):
        data=self.events[0]['data']
        data.update(mode='physical',status='REFUSED',reason='PACKAGE_IDENTITY_MISMATCH')
        self.supervisor['child_exit_code']=2
        result=control.analyze_events(self.events,self.supervisor,self.capture,self.nonce,'physical')
        self.assertEqual(result['verdict'],'CONTROL_LANE_REFUSED')
        self.assertEqual(result['provider_capability'],'ACCEPTED_CANONICAL_BASELINE')

    def test_api_success_alone_not_observation(self):
        data=self.events[0]['data']
        data.update(mode='physical',provider_evaluated=True,api_success=True)
        result=control.analyze_events(self.events,self.supervisor,self.capture,self.nonce,'physical')
        self.assertEqual(result['verdict'],'EVIDENCE_INVALID')

    def test_failed_support_metadata_remains_failure(self):
        rows=[]
        for name,result in (('slInit',0),('slSetD3DDevice',0),('slIsFeatureSupported',32)):
            rows.append(self.row('api_enter',{'name':name},len(rows)))
            rows.append(self.row('api_return',{'name':name,'result':result},len(rows)))
        metadata={'feature':1004,'feature_loaded':{'result':34,'loaded':None},'requirements':{'export_available':False}}
        rows.append(self.row('support_diagnostics',metadata,len(rows)))
        terminal=copy.deepcopy(self.events[-1]['data']);terminal.update(mode='physical',status='FAILED')
        rows.append(self.row('terminal',terminal,len(rows)));self.supervisor['child_exit_code']=3
        result=control.analyze_events(rows,self.supervisor,self.capture,self.nonce,'physical')
        self.assertTrue(result['trace_valid'],result['errors'])
        self.assertEqual(result['verdict'],'CONTROL_LANE_FAILED')
        self.assertEqual(result['support_diagnostics'],metadata)
        self.assertFalse(result['provider_evaluated'])
        rows[5]['data']['result']=0
        result=control.analyze_events(rows,self.supervisor,self.capture,self.nonce,'physical')
        self.assertFalse(result['trace_valid'])
        self.assertEqual(result['verdict'],'EVIDENCE_INVALID')


class ReadbackTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.expected=bytes.fromhex('00340038003a003c')*4096
        for name in ('input','output','consumer','expected-copy'):(self.root/(name+'.rgba16f')).write_bytes(self.expected)
        (self.root/'guard.rgba16f').write_bytes(bytes(32768))
        (self.root/'depth.r32f').write_bytes(bytes.fromhex('0000803f')*4096)
        (self.root/'motion.rg16f').write_bytes(bytes(16384))

    def test_independent_copy_relationship(self):
        control.validate_readback(self.root,'warp')

    def test_unchanged_output_cannot_be_attributed(self):
        for name in ('output','consumer'):(self.root/(name+'.rgba16f')).write_bytes(bytes(32768))
        with self.assertRaisesRegex(ValueError,'UNCHANGED_OUTPUT_REJECTED'):control.validate_readback(self.root,'physical')

    def test_equal_wrong_inputs_do_not_define_oracle(self):
        for name in ('input','expected-copy'):(self.root/(name+'.rgba16f')).write_bytes(b'x'*32768)
        with self.assertRaisesRegex(ValueError,'INDEPENDENT_INPUT'):control.validate_readback(self.root,'warp')

    def test_wrong_consumer_copy(self):
        (self.root/'consumer.rgba16f').write_bytes(b'x'*32768)
        with self.assertRaisesRegex(ValueError,'CONSUMER_COPY'):control.validate_readback(self.root,'physical')

    def test_guard_was_written(self):
        (self.root/'guard.rgba16f').write_bytes(self.expected)
        with self.assertRaisesRegex(ValueError,'GUARD_CHANGED'):control.validate_readback(self.root,'physical')

    def test_warp_changed_pixels_are_not_correct_copy(self):
        for name in ('output','consumer'):(self.root/(name+'.rgba16f')).write_bytes(b'x'*32768)
        with self.assertRaisesRegex(ValueError,'WARP_COPY'):control.validate_readback(self.root,'warp')


class PhysicalTraceTests(unittest.TestCase):
    setUp=EvidenceTests.setUp
    row=EvidenceTests.row

    def fixture(self):
        rows=[]
        def add(name,data):rows.append(self.row(name,data,len(rows)))
        def api(name):add('api_enter',{'name':name});add('api_return',{'name':name,'result':0})
        modules=[]
        for r in control.canonical_manifest()['files']:
            name=Path(r['path']).name
            if name in ('sl.interposer.dll','sl.common.dll','sl.dlss_nr.dll','nvngx_dlssnr.dll'):
                modules.append({'mapped_path':'C:\\Canonical\\'+name,'backing_bytes':r['bytes'],'backing_sha256':r['sha256'],'file_version':'2.13.0.0'})
        def module(phase):add('module_identity',{'phase':phase,'errors':[],'notifications_complete':True,'modules':copy.deepcopy(modules)})
        scene=json.loads((ROOT/'tests/nr/lab/fixtures/streamline_control_scene.json').read_text(encoding='utf-8-sig'))
        def stage(fence):
            add('submission_enter',{'fence':fence,'queue_identity':100,'command_list_identity':scene['resources']['command_list_identity'] if fence==2 else fence+200})
            add('lifecycle',{'phase':'submitted','fence':fence})
            add('lifecycle',{'phase':'completed','fence':fence,'completed_fence':fence,'device_removed_reason':0})
        module('before_load');module('after_load')
        add('preferences',{'feature':1004,'sdk_version_passed':2<<48|14<<32|1<<16|0xfedc})
        add('device',{k:scene['resources'][k] for k in ('device_identity','adapter_luid')})
        for name in control.APIS[:5]:
            api(name)
            if name=='slInit':module('after_init')
            if name=='slSetD3DDevice':module('after_device')
        add('feature_loaded',{'loaded':True});stage(1)
        for name in ('resources','constants','options'):add(name,scene[name])
        api('slGetNewFrameToken');add('frame',{'frame_id':1,'viewport':1,'token_identity':876,'reset':True})
        for name in ('slSetConstants','slDLSSNRSetOptions','slSetTagForFrame'):api(name)
        module('before_eval');api('slEvaluateFeature');module('after_eval')
        stage(2);stage(3);add('lifecycle',{'phase':'drained'})
        add('readback',{'independent_copy_checks':True,'unchanged_output_negative_rejected':True})
        for name in control.APIS[-2:]:api(name)
        module('after_shutdown')
        add('terminal',{'mode':'physical','status':'OBSERVED','provider_evaluated':True,'api_success':True,
            'gpu_completed':True,'output_attribution':True,'model_correctness':'UNKNOWN','control_pass':False})
        return rows

    def check_physical(self,rows):
        return control.analyze_events(rows,self.supervisor,self.capture,self.nonce,'physical','C:\\Canonical')

    def test_complete_fixture_remains_no_model_pass(self):
        r=self.check_physical(self.fixture())
        self.assertTrue(r['trace_valid'],r['errors']);self.assertFalse(r['control_pass'])
        self.assertEqual(r['model_correctness'],'UNKNOWN')

    def test_set_options_failure_cannot_be_success(self):
        rows=self.fixture()
        next(r['data'] for r in rows if r['event']=='api_return' and r['data']['name']=='slDLSSNRSetOptions')['result']=7
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_evaluate_failure_cannot_be_success(self):
        rows=self.fixture()
        next(r['data'] for r in rows if r['event']=='api_return' and r['data']['name']=='slEvaluateFeature')['result']=7
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_removed_device_fence_not_completed(self):
        rows=self.fixture()
        next(r['data'] for r in rows if r['event']=='lifecycle' and r['data']['phase']=='completed')['completed_fence']=2**64-1
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_no_drain_no_free_claim(self):
        rows=self.fixture();rows=[r for r in rows if not(r['event']=='lifecycle' and r['data']['phase']=='drained')]
        for i,r in enumerate(rows):r['seq']=i
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_runtime_hash(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='module_identity')['modules'][0]['backing_sha256']='0'*64
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_runtime_path(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='module_identity')['modules'][0]['mapped_path']='C:\\Other\\sl.interposer.dll'
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_lost_transient_notifications(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='module_identity')['notifications_complete']=False
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_frame(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='frame')['viewport']=2
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_missing_resources(self):
        rows=[r for r in self.fixture() if r['event']!='resources']
        for i,r in enumerate(rows):r['seq']=i
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_resource_device(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='resources')['resources'][0]['device_identity']=1
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_options(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='options')['preset']=0
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_wrong_projection(self):
        rows=self.fixture();next(r['data'] for r in rows if r['event']=='constants')['cameraViewToClip'][2][2]=0
        self.assertFalse(self.check_physical(rows)['trace_valid'])

    def test_free_begins_before_drain(self):
        rows=self.fixture();entry=next(r for r in rows if r['event']=='api_enter' and r['data']['name']=='slFreeResources')
        rows.remove(entry);index=next(i for i,r in enumerate(rows) if r['event']=='lifecycle' and r['data']['phase']=='drained')
        rows.insert(index,entry)
        for i,r in enumerate(rows):r['seq']=i
        self.assertFalse(self.check_physical(rows)['trace_valid'])


if __name__=='__main__': unittest.main()
