from pathlib import Path
import copy
import sys
import tempfile
import json
import unittest

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/nr/lab/streamline'))
sys.path.insert(0,str(ROOT/'tools/nr/lab/provider'))
from provider_identity import canonical_manifest


class SetupEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory=r'C:\fixture\matched'
        self.nonce='1'*32
        self.supervisor={'process_status':'exited','child_pid':321,'child_exit_code':0,'child_drained':True,'timed_out':False,'unhandled_exception':None}
        self.capture={'capture_complete':True,'stdout':{'truncated':False},'stderr':{'truncated':False}}
        self.events=[]
        def add(event,data):
            self.events.append({'schema_version':1,'seq':len(self.events),'nonce':self.nonce,'pid':321,'created_filetime':123456,'event':event,'data':data})
        self.add=add
        def snap(phase,names):
            modules=[]
            for i,name in enumerate(names):
                row=next(r for r in canonical_manifest()['files'] if r['path'].endswith('/'+name))
                modules.append({'mapped_path':self.directory+'\\'+name,'module_base':4096*(i+1),'backing_bytes':row['bytes'],'backing_sha256':row['sha256']})
            add('snapshot',{'phase':phase,'modules':modules,'errors':[]})
        add('preflight',{'verified':True})
        snap('before_load',[])
        add('load',{'loaded':True,'path':self.directory+r'\sl.interposer.dll'})
        snap('after_load',['sl.interposer.dll'])
        add('init_enter',{'sdk_version':(2<<48)|(11<<32)|(1<<16)|0xfedc,'feature':1004,'flags':0,'plugin_directory':self.directory})
        add('init_return',{'result':0})
        snap('after_init',['sl.interposer.dll','sl.common.dll','sl.dlss_nr.dll'])
        add('feature_loaded',{'attempted':False,'result':None,'loaded':None,'reason':'D3D_DEVICE_NOT_CREATED_IN_SETUP_PROBE'})
        add('requirements',{'result':0,'required_tags':[70,71]})
        add('shutdown_enter',{})
        add('shutdown_return',{'result':0})
        snap('after_shutdown',['sl.interposer.dll'])
        add('terminal',{'setup_status':'OBSERVED','evaluation':'NOT_IMPLEMENTED','control_pass':False})

    def analyze(self):
        from setup_probe import analyze_events
        return analyze_events(self.events,self.supervisor,self.capture,self.directory,self.nonce)

    def test_setup_never_becomes_working_control(self):
        result=self.analyze()
        self.assertTrue(result['trace_valid'])
        self.assertTrue(result['setup_observed'])
        self.assertEqual(result['verdict'],'BLOCKED_NR_HOST_INTERFACE')
        self.assertFalse(result['control_pass'])
        self.assertFalse(result['evaluation_attempted'])

    def test_foreign_matched_module_path_rejected(self):
        self.events[3]['data']['modules'][0]['mapped_path']=r'C:\foreign\sl.interposer.dll'
        self.assertFalse(self.analyze()['trace_valid'])

    def test_wrong_backing_hash_rejected(self):
        self.events[6]['data']['modules'][2]['backing_sha256']='0'*64
        self.assertFalse(self.analyze()['trace_valid'])

    def test_unknown_or_duplicate_vendor_module_rejected(self):
        modules=self.events[6]['data']['modules']
        modules.append(copy.deepcopy(modules[0]))
        self.assertFalse(self.analyze()['trace_valid'])

    def test_changed_process_or_nonce_rejected(self):
        for field,value in [('pid',999),('nonce','2'*32),('created_filetime',777),('seq',999)]:
            with self.subTest(field=field):
                old=self.events[5][field];self.events[5][field]=value
                self.assertFalse(self.analyze()['trace_valid']);self.events[5][field]=old

    def test_eval_claim_or_unrecognized_stage_rejected(self):
        self.events[-1]['data']['control_pass']=True
        self.assertFalse(self.analyze()['trace_valid'])
        self.events[-1]['data']['control_pass']=False
        self.events[8]['event']='evaluate'
        self.assertFalse(self.analyze()['trace_valid'])

    def test_missing_shutdown_is_incomplete(self):
        self.events=[e for e in self.events if e['event']!='shutdown_return']
        for i,e in enumerate(self.events):e['seq']=i
        self.assertFalse(self.analyze()['setup_observed'])

    def test_init_failure_is_setup_failure_not_provider_capability(self):
        self.events[5]['data']['result']=17
        self.events[-1]['data']['setup_status']='FAILED'
        self.supervisor['child_exit_code']=3
        result=self.analyze()
        self.assertEqual(result['verdict'],'SETUP_FAILED')
        self.assertEqual(result['provider_capability'],'ACCEPTED_CANONICAL_BASELINE')

    def test_timeout_or_capture_loss_prevents_setup_claim(self):
        self.supervisor['timed_out']=True
        self.assertFalse(self.analyze()['trace_valid'])
        self.supervisor['timed_out']=False
        self.capture['stderr']['truncated']=True
        self.assertFalse(self.analyze()['trace_valid'])

    def test_shutdown_error_prevents_setup_claim(self):
        self.events[10]['data']['result']=1
        self.events[-1]['data']['setup_status']='FAILED'
        self.supervisor['child_exit_code']=3
        self.assertEqual(self.analyze()['verdict'],'SETUP_FAILED')

    def test_reordered_initialization_cannot_pass(self):
        self.events[4],self.events[5]=self.events[5],self.events[4]
        for i,e in enumerate(self.events):e['seq']=i
        self.assertFalse(self.analyze()['trace_valid'])

    def test_failed_terminal_cannot_hide_incomplete_lifecycle(self):
        self.events=[e for e in self.events if e['event'] in ('preflight','init_enter','terminal')]
        for i,e in enumerate(self.events):e['seq']=i
        self.events[-1]['data']['setup_status']='FAILED'
        self.supervisor['child_exit_code']=3
        self.assertFalse(self.analyze()['trace_valid'])

    def test_failed_load_cannot_be_observed_setup(self):
        self.events[2]['data']['loaded']=False
        self.assertFalse(self.analyze()['trace_valid'])

    def test_wrong_plugin_directory_or_bypass_flags_rejected(self):
        for field,value in [('plugin_directory',r'C:\mixed'),('feature',18),('flags',8),('flags',4),('sdk_version',None)]:
            with self.subTest(field=field,value=value):
                old=self.events[4]['data'][field];self.events[4]['data'][field]=value
                self.assertFalse(self.analyze()['trace_valid']);self.events[4]['data'][field]=old

    def test_rehashed_foreign_launcher_cannot_hide_executed_source(self):
        import setup_probe
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            names=['native/StreamlineSetupProbe.exe','lab/LabSupervisor.exe','tools/nr/lab/provider/canonical-manifest.json',
                   'tools/nr/lab/streamline/setup_probe.py','tools/nr/lab/campaign.py','tools/nr/lab/provider/provider_identity.py',
                   'tools/nr/lab/streamline/native/SetupProbe.cpp','external/streamline/sl_core_api.h',
                   'external/streamline/sl_core_types.h','external/streamline/sl_version.h','SOURCE_BUILD_RECEIPT.json']
            for name in names:
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(b'inert fixture')
            setup_probe.save(root/'SETUP_PACKAGE.json',{'schema_version':1,'scope':'STREAMLINE_SETUP_ONLY_NO_EVALUATION',
                'files':[{'path':n,'sha256':setup_probe.sha(root/n)} for n in names]})
            with self.assertRaisesRegex(ValueError,'EXECUTED_SOURCE_MISMATCH'):
                setup_probe.verify_probe(root)

    def test_missing_consent_refuses_before_creating_output(self):
        import setup_probe
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'run'
            with self.assertRaisesRegex(ValueError,'EXPLICIT_PROVIDER_INIT_CONSENT_REQUIRED'):
                setup_probe.run(Path(temp),Path(temp)/'absent.zip',Path(temp)/'absent',output,False)
            self.assertFalse(output.exists())

    def test_return_without_entry_cannot_hide_provider_activity(self):
        self.events=[e for e in self.events if e['event']!='init_enter']
        for i,e in enumerate(self.events):e['seq']=i
        self.events[-1]['data']['setup_status']='FAILED'
        self.supervisor['child_exit_code']=3
        self.assertFalse(self.analyze()['trace_valid'])


if __name__=='__main__':unittest.main()
