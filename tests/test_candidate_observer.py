import importlib.util
import json
import copy
import hashlib
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TOOL=ROOT/'tools/diagnostics/candidate_observer.py'
def event(sequence=1,token=1,predecessor=0,format=40,flags=2):
    descriptor=json.loads((ROOT/'tools/diagnostics/candidate_observer/candidate_examples.json').read_text())['examples'][0]['record']['descriptor']
    descriptor['format']['value']=format;descriptor['resource_flags']['value']=flags
    return {'type':'event','schema_version':'2.0','sequence':str(sequence),'epoch':'1','window':'1',
            'elapsed_ms':'0','kind':'create_committed','descriptor':descriptor,'descriptor_known':True,
            'result':0,'token':str(token),'predecessor':str(predecessor),'source':'D3D12_COMMITTED_NORMAL',
            'heap_offset':None,'native_observation':'0','present_observation':'0','coverage_mask':3}
def header():
    return {'type':'header','schema_version':'2.0','profile_id':'OBS-CLOSURE-1','session':'c'*32,
            'redaction':'session_local_observation_tokens','source_identity':'unavailable',
            'protection_policy':'unknown','limits':json.loads((ROOT/'tools/diagnostics/candidate_observer/limits.json').read_text())}

class AnalyzerTests(unittest.TestCase):
    def budget_footer(self):
        reasons=['session_data','session_control','window_data','window_control','window_quota_slots',
                 'resource_window','frame_streams','frame_order','frame_quota_slots','resource_frame','frame_data']
        return {'type':'footer','schema_version':'2.0','last_sequence':'1','enqueued':'1','serialized':'1',
                'dropped':'3','drop_exact':True,'loss_mask':8,'high_water':1,'reason':7,'complete_transport':False,
                'budget_refusals':{'profile':'quota-reasons-1','reasons':[
                    {'reason':r,'count':'3' if r=='window_quota_slots' else '0','exact':True} for r in reasons]}}

    def test_budget_diagnostics_round_trip_and_pack(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl';footer=self.budget_footer()
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),event(),footer))+b'\n')
            report=m.analyze(trace,p/'out')
            self.assertEqual(report['budget_refusals'],footer['budget_refusals'])
            self.assertFalse(report['complete_transport'])
            self.assertIn(b'window_quota_slots: 3', (p/'out/report.txt').read_bytes())
            m.pack(p/'out',p/'report.zip')

    def test_budget_diagnostics_reject_contradictions_and_private_fields(self):
        m=self.tool()
        mutations=[lambda f:f['budget_refusals']['reasons'][4].update(count='4'),
                   lambda f:f['budget_refusals']['reasons'][4].update(count='0'),
                   lambda f:f.update(loss_mask=0),
                   lambda f:f['budget_refusals']['reasons'][4].update(exact=1),
                   lambda f:f['budget_refusals'].update(resource_address='0xdead'),
                   lambda f:f['budget_refusals']['reasons'][4].update(reason='private-path'),
                   lambda f:f['budget_refusals'].update(profile='other'),
                   lambda f:f['budget_refusals']['reasons'].pop()]
        for mutate in mutations:
            with self.subTest(mutation=mutate), tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
                p=Path(td);trace=p/'trace.jsonl';footer=self.budget_footer();mutate(footer)
                trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),event(),footer))+b'\n')
                with self.assertRaises(m.Invalid):m.analyze(trace,p/'out')

    def test_budget_diagnostics_inexact_mixed_loss_and_complete_refusal(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl';footer=self.budget_footer()
            footer['budget_refusals']['reasons'][4].update(count='2',exact=False)
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),event(),footer))+b'\n')
            report=m.analyze(trace,p/'inexact');self.assertFalse(report['complete_transport'])
            self.assertIn(b'lower bound',(p/'inexact/report.txt').read_bytes())
            with self.assertRaises(m.Invalid):m.validate_budget_refusals(report['budget_refusals'],complete=True)
            footer['budget_refusals']['reasons'][4].update(count='2',exact=True);footer['loss_mask']=12
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),event(),footer))+b'\n')
            self.assertFalse(m.analyze(trace,p/'mixed')['complete_transport'])

    def test_pack_refuses_budget_count_above_exact_family_losses(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl';footer=self.budget_footer()
            footer['families']=[{'enqueued':'1' if i==1 else '0','dropped':'3' if i==1 else '0','drop_exact':True} for i in range(4)]
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),event(),footer))+b'\n')
            m.analyze(trace,p/'out');rp=p/'out/report.json';report=m.strict_json(rp.read_bytes())
            report['budget_refusals']['reasons'][4]['count']='4';rp.write_bytes(m.canonical(report)+b'\n')
            # Mutate report + text + inventory consistently so semantic validation must refuse it.
            text=p/'out/report.txt';text.write_bytes(m.report_text(m.strict_json((p/'out/candidates.json').read_bytes()),report))
            inventory=p/'out/inventory.json';items=m.strict_json(inventory.read_bytes())
            for changed in (rp,text):items[changed.name]={'bytes':changed.stat().st_size,'sha256':hashlib.sha256(changed.read_bytes()).hexdigest()}
            inventory.write_bytes(m.canonical(items)+b'\n')
            with self.assertRaises(m.Invalid):m.pack(p/'out',p/'bad.zip')


    def tool(self):
        self.assertTrue(TOOL.exists(), 'offline analyzer is not implemented')
        spec=importlib.util.spec_from_file_location('observer_analyzer',TOOL)
        module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        return module

    def fixture(self):
        return json.loads((ROOT/'tools/diagnostics/candidate_observer/candidate_examples.json').read_text())['examples'][0]['record']

    def test_example_is_observation_only_and_probability_mutation_refuses(self):
        m=self.tool(); c=self.fixture();m.validate_candidate(c)
        c['ranking']['score_as_probability']=True
        with self.assertRaises(m.Invalid):m.validate_candidate(c)

    def test_depth_positive_does_not_admit_and_r32float_is_negative(self):
        m=self.tool();e=event(format=40,flags=2)
        c=m.reduce_events([e], 'c'*32, True)[0][0]
        self.assertEqual(c['local_stage'],'CANDIDATE');self.assertFalse(c['may_admit'])
        e=event(format=41,flags=0)
        c=m.reduce_events([e], 'c'*32, True)[0][0]
        self.assertEqual(c['local_stage'],'OBSERVED');self.assertEqual(c['hypotheses'],[])

    def test_distinct_creation_ids_and_duplicate_integrity(self):
        m=self.tool();a=event();b=event(sequence=2,token=2,predecessor=1)
        records,stats=m.reduce_events([a,a,b],'c'*32,True)
        self.assertEqual(len(records),2);self.assertEqual(stats['duplicates'],1)
        self.assertNotEqual(records[0]['candidate_key'],records[1]['candidate_key'])
        self.assertEqual(records[1]['retention']['state'],'SUPERSEDED_FOR_CORRELATION')
        for c in records:m.validate_candidate(c)
        altered=dict(a);altered['result']=1
        with self.assertRaises(m.Invalid):m.reduce_events([a,altered],'c'*32,True)
        with self.assertRaises(m.Invalid):m.reduce_events([b,a],'c'*32,True)

    def test_unknown_schema_duplicate_keys_and_native_address_refuse(self):
        m=self.tool()
        with self.assertRaises(m.Invalid):m.strict_json('{"a":1,"a":2}')
        a=event();a['resource_address']='0xdead'
        with self.assertRaises(m.Invalid):m.reduce_events([a],'c'*32,True)

    def test_trace_determinism_missing_footer_and_pack(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl'
            h=header();e=event()
            trace.write_bytes(m.canonical(h)+b'\n'+m.canonical(e)+b'\n')
            first=m.analyze(trace,p/'a');second=m.analyze(trace,p/'b')
            self.assertFalse(first['complete_transport'])
            self.assertEqual((p/'a/candidates.json').read_bytes(),(p/'b/candidates.json').read_bytes())
            m.pack(p/'a',p/'report.zip')
            with self.assertRaises(m.Invalid):m.pack(p/'a',p/'report.zip')
            with self.assertRaises(m.Invalid):m.analyze(trace,p/'a')

    def test_nested_metadata_and_contradictory_outcomes_refuse(self):
        m=self.tool();a=event();a['heap']={'known':False,'path':'private-path'}
        with self.assertRaises(m.Invalid):m.validate_event(a)
        a=event();a['source']='D3D12_IGD_ALTERNATE'
        with self.assertRaises(m.Invalid):m.validate_event(a)
        a=event();a['descriptor_known']=False
        with self.assertRaises(m.Invalid):m.validate_event(a)

    def test_expiry_is_observer_eviction_and_late_predecessor_unavailable(self):
        m=self.tool();a=event();b=event(sequence=2,token=2,predecessor=1);b['elapsed_ms']='10000'
        records,stats=m.reduce_events([a,b],'c'*32,True)
        self.assertEqual(len(records),1);self.assertEqual(stats['evicted'],1)
        self.assertEqual(records[0]['correlation']['prior_address_history'],'unavailable')

    def test_gap_never_claims_complete_and_clock_window_domains_refuse(self):
        m=self.tool();a=event();b=event(sequence=3,token=2)
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl'
            footer={'type':'footer','schema_version':'2.0','last_sequence':'3','enqueued':'2','serialized':'2',
                    'dropped':'0','drop_exact':True,'loss_mask':0,'high_water':2,'reason':1,'complete_transport':True}
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in (header(),a,b,footer))+b'\n')
            self.assertFalse(m.analyze(trace,p/'out')['complete_transport'])
        a['window']='2';b['window']='1'
        with self.assertRaises(m.Invalid):m.reduce_events([a,b],'c'*32,True)
        a=event();a['descriptor']['mip_levels']['value']=65536
        with self.assertRaises(m.Invalid):m.validate_event(a)

    def test_candidate_joins_factors_and_native_width_domains_refuse(self):
        m=self.tool();base=m.candidate(event(sequence=2,token=2),'c'*32,False)
        bad=copy.deepcopy(base);bad['correlation']['previous_candidate']={'session':'d'*32,'api':'D3D12','birth_epoch':'1','creation_sequence':'1'}
        with self.assertRaises(m.Invalid):m.validate_candidate(bad)
        bad=copy.deepcopy(base);bad['factor_ledger'][0]['outcome']='unknown'
        with self.assertRaises(m.Invalid):m.validate_candidate(bad)
        bad=copy.deepcopy(base);bad['descriptor']['mip_levels']['value']=65536
        bad['descriptor_fingerprint']['value']=hashlib.sha256(m.canonical({'descriptor_schema':'2.0','descriptor':bad['descriptor']})).hexdigest()
        with self.assertRaises(m.Invalid):m.validate_candidate(bad)

    def test_pack_refuses_self_consistent_private_report(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl';trace.write_bytes(m.canonical(header())+b'\n'+m.canonical(event())+b'\n')
            m.analyze(trace,p/'out');report=p/'out/report.json'
            value=m.strict_json(report.read_bytes());value['resource_address']='0xdead';report.write_bytes(m.canonical(value)+b'\n')
            inventory=p/'out/inventory.json';items=m.strict_json(inventory.read_bytes())
            items['report.json']={'bytes':report.stat().st_size,'sha256':hashlib.sha256(report.read_bytes()).hexdigest()}
            inventory.write_bytes(m.canonical(items)+b'\n')
            with self.assertRaises(m.Invalid):m.pack(p/'out',p/'pack.zip')

    def test_footer_family_loss_and_incomplete_framing_refuse(self):
        m=self.tool()
        with tempfile.TemporaryDirectory(dir=ROOT/'builds/candidate-observer') as td:
            p=Path(td);trace=p/'trace.jsonl'
            footer={'type':'footer','schema_version':'2.0','last_sequence':'1','enqueued':'1','serialized':'1','dropped':'0',
                    'drop_exact':True,'loss_mask':0,'high_water':1,'reason':1,'complete_transport':True,
                    'families':[{'enqueued':'1' if i==1 else '0','dropped':'1' if i==1 else '0','drop_exact':True} for i in range(4)]}
            rows=[header(),event(),footer];trace.write_bytes(b'\n'.join(m.canonical(v) for v in rows)+b'\n')
            with self.assertRaises(m.Invalid):m.analyze(trace,p/'out')
            footer['families'][1]['dropped']='0';footer['families'][1]['drop_exact']=False
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in rows)+b'\n')
            self.assertFalse(m.analyze(trace,p/'out')['complete_transport'])
            trace.write_bytes(b'\n'.join(m.canonical(v) for v in rows))
            with self.assertRaises(m.Invalid):m.read_trace(trace)
            trace.write_bytes((json.dumps(header())+'\n').encode('utf-16'))
            with self.assertRaises(m.Invalid):m.read_trace(trace)
        with self.assertRaises(m.Invalid):m.strict_json('1e9999')

    def test_complete_candidate_cannot_have_loss_or_missing_footer(self):
        m=self.tool();base=m.candidate(event(),'c'*32,True)
        for change in ({'loss_observed':True},{'drop_count':'1'},{'drop_count_exact':False},
                       {'limitations':['missing_footer']}):
            bad=copy.deepcopy(base);bad['coverage'].update(change)
            with self.assertRaises(m.Invalid):m.validate_candidate(bad)

if __name__=='__main__':unittest.main()
