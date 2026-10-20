import copy
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from tools.nr_coverage.adapters import fact, import_trace, manual_envelope
from tools.nr_coverage.identity import canonical,digest,evidence_key,excluding_hash,review_key
from tools.nr_coverage.import_pipeline import import_normalized,safe_relative_path
from tools.nr_coverage.lab_cases import synthetic_envelope,query_for,run_lab_case
from tools.nr_coverage.migration import migrate_claims
from tools.nr_coverage.policy import register_policy,record_review
from tools.nr_coverage.projection import project
from tools.nr_coverage.schema import CoverageError,strict_load
from tools.nr_coverage.store import Store

class BoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = Store(self.root / 'store'); self.addCleanup(self.store.close)
        self.raw = b'Actual synthetic owner API test receipt\n'
        self.env = synthetic_envelope(self.raw)

    def publish(self, env=None, **options):
        return import_normalized(canonical(env or self.env),self.raw,self.store,self.root,trusted_producer='synthetic-cpu-harness',**options)

    def artifact(self, env=None):
        env = env or self.env
        data = b'Synthetic CPU oracle receipt, not a renderer owner result'
        (self.root / 'oracle.bin').write_bytes(data)
        env['record']['artifacts'] = [{'artifact_id':'oracle','relative_path':'oracle.bin','sha256':fact(digest(data)),'bytes':fact(len(data)),'role':'protocol_oracle','availability':'supplied','visibility':'synthetic_public'}]
        env['record']['outcomes']['protocol']['evidence_ids'] = ['oracle']
        env['record']['evidence_id'] = evidence_key(env['record'])

    def policy(self):
        policy = {'schema_version':'2.0.0','kind':'qualification_policy','policy_id':'synthetic:cpu-protocol','revision':'1','policy_sha256':'',
                  'scope':copy.deepcopy(self.env['record']['scope']),'lane':'cpu_fixture','intent':'positive_execution','mode':'qualify_scoped',
                  'required_obligations':['evd:protocol'],'components':['neurotic'],'not_applicable_justifications':[],
                  'rules':[{'obligation_id':'evd:protocol','axis':'protocol','predicate':'AXIS_PASS','operand':'protocol','expected':'PASS'},
                           {'obligation_id':'evd:protocol','axis':'protocol','predicate':'ARTIFACT_VERIFIED','operand':'protocol_oracle','expected':'verified'}],
                  'review_basis_artifact_hashes':[],'runtime_authority':False}
        policy['policy_sha256'] = excluding_hash(policy,'policy_sha256')
        return policy

    def review(self, policy, action='ACCEPT_SCOPE'):
        event = {'schema_version':'2.0.0','kind':'review_event','review_id':'','target_evidence_id':self.env['record']['evidence_id'],
                 'action':action,'actor_ref':'local-reviewer','occurred_utc':'2026-09-26T00:00:00Z','scope_sha256':self.env['record']['scope']['scope_sha256']['value'],
                 'policy_sha256':policy['policy_sha256'],'basis_artifact_hashes':[],'supersedes_review_ids':[],'related_evidence_ids':[],'reason':'Synthetic offline owner test'}
        event['review_id'] = review_key(event); return event

    def test_windows_reserved_and_traversal_paths(self):
        for path in ('../a','a/../b','C:/a','a\\b','/a','a//b','a/.','a.','a ','NUL','CON.txt','aux.png','a/COM1.log','LPT\u00b9.txt','CONIN$','x:y'):
            self.assertFalse(safe_relative_path(path),path)
        self.assertTrue(safe_relative_path('logs/output.json'))

    def test_supplied_artifact_verified_in_cas(self):
        self.artifact()
        result = self.publish()
        self.assertEqual('PUBLISHED',result['status'])
        row = self.store.db.execute('SELECT owned_blob_key FROM artifacts').fetchone()
        self.assertEqual((self.root/'oracle.bin').read_bytes(),(self.store.root/row[0]).read_bytes())

    def test_artifact_hash_mismatch_does_not_publish(self):
        self.artifact(); (self.root/'oracle.bin').write_bytes(b'X' * self.env['record']['artifacts'][0]['bytes']['value'])
        result = self.publish()
        self.assertEqual(['ARTIFACT_HASH_MISMATCH'],result['reason_codes'])
        self.assertEqual(0,self.store.count('evidence'))

    def test_missing_supplied_and_missing_optional_are_distinct(self):
        self.artifact(); (self.root/'oracle.bin').unlink()
        self.assertEqual(['ARTIFACT_MISSING'],self.publish()['reason_codes'])
        self.env['record']['artifacts'][0]['availability']='missing'
        self.env['record']['evidence_id']=evidence_key(self.env['record'])
        self.assertEqual('PUBLISHED',self.publish()['status'])
        self.assertEqual(0,self.store.count('artifacts'))

    def test_imported_acceptance_is_only_claim(self):
        self.env['record']['qualification_claim']['claimed_level']='scope_reviewed'
        self.env['record']['evidence_id']=evidence_key(self.env['record'])
        self.assertEqual('PUBLISHED',self.publish()['status'])
        self.assertEqual(0,self.store.count('reviews'))
        self.assertEqual('INCONCLUSIVE',project(query_for(self.env),self.store)['axes']['protocol']['status'])

    def test_manual_cannot_claim_executed_receipt(self):
        self.env['record']['source']['kind']='manual_report'; self.env['record']['evidence_id']=evidence_key(self.env['record'])
        self.assertEqual(['BASIS_CONTRADICTION'],self.publish()['reason_codes'])

    def test_raw_missing_becomes_report(self):
        result=import_normalized(canonical(self.env),None,self.store)
        self.assertEqual('PUBLISHED',result['status']); self.assertEqual('REPORTED',result['intake_class'])
        record=strict_load(self.store.db.execute('SELECT record_json FROM evidence').fetchone()[0])
        self.assertIn('SOURCE_UNVERIFIED',record['limitations'][0])

    def test_local_review_requires_real_artifacts(self):
        self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer')
        with self.assertRaisesRegex(CoverageError,'QUALIFICATION_UNMET'): record_review(self.review(policy),self.store,local_actor='local-reviewer')

    def test_not_run_never_qualifies_even_with_pass_and_oracle(self):
        self.artifact(); self.env['record']['termination']='not_run'; self.env['record']['evidence_id']=evidence_key(self.env['record'])
        self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer')
        with self.assertRaisesRegex(CoverageError,'QUALIFICATION_UNMET'): record_review(self.review(policy),self.store,local_actor='local-reviewer')

    def test_three_controls_remain_distinct(self):
        from tools.nr_coverage.identity import refresh_cell
        keys=[]
        for mode in ('pristine_unloaded','loaded_nr_off','loaded_nr_on'):
            cell=copy.deepcopy(self.env['cell']); cell['identity_extensions']['control_mode']=fact(mode)
            keys.append(refresh_cell(cell)['compatibility_cell_id'])
        self.assertEqual(3,len(set(keys)))

    def test_invalid_reference_does_not_erase_protocol(self):
        self.artifact(); self.env['record']['controls']['reference_validity']='invalid'; self.env['record']['evidence_id']=evidence_key(self.env['record'])
        self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer'); record_review(self.review(policy),self.store,local_actor='local-reviewer')
        query=query_for(self.env); query['policy_sha256']=fact(policy['policy_sha256'])
        self.assertEqual('CHECK_PASSED',project(query,self.store)['axes']['protocol']['status'])

    def test_legacy_record_mapping_preserves_basis_and_unknown_extensions(self):
        from tools.nr_coverage.adapters import legacy_envelope,unknown
        schema=json.loads((Path(__file__).parents[2]/'tools'/'nr_coverage'/'schemas'/'legacy_runtime_schema_v1.json').read_text())
        v2=self.env['record']
        legacy={key:copy.deepcopy(v2[key]) for key in ('basis','lane','intent','started_utc','ended_utc','stages','outcomes','trace_integrity','required_obligations','fulfilled_obligations','missing_obligations','termination','limitations')}
        legacy.update(schema_version='1.0.0',run_id='old-run',cell_key=self.env['cell']['condition']['cell_key'],cohort_key=self.env['cell']['cohort_key'],cohort=self.env['cell']['cohort'],scenario_id='old-scenario',scenario_revision='1',duration_ms=unknown(),
                      observation_scope={k:unknown() for k in schema['properties']['observation_scope']['properties']},checks_enabled={k:unknown() for k in schema['properties']['checks_enabled']['properties']},artifacts=[],review_state='accepted_for_scope',reviewer_ref=unknown(),policy_revision='old',runtime_acceptance_granted=False)
        result=legacy_envelope(canonical(legacy),self.env['cell']['condition'])
        self.assertTrue(all(v['state']=='unknown' for v in result['cell']['identity_extensions'].values()))
        self.assertEqual(legacy['cell_key'],result['cell']['condition']['cell_key'])
        self.assertEqual('executed_receipt',result['record']['basis'])
        self.assertEqual('structured',result['record']['qualification_claim']['claimed_level'])

    def test_review_then_contest_then_explicit_resolution(self):
        self.artifact(); self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer')
        accept=self.review(policy); record_review(accept,self.store,local_actor='local-reviewer')
        query=query_for(self.env); query['policy_sha256']=fact(policy['policy_sha256'])
        self.assertEqual('CHECK_PASSED',project(query,self.store)['axes']['protocol']['status'])
        contest=self.review(policy,'CONTEST'); record_review(contest,self.store,local_actor='local-reviewer')
        self.assertEqual('CONFLICTING',project(query,self.store)['axes']['protocol']['status'])
        resolution=self.review(policy); resolution['supersedes_review_ids']=[accept['review_id'],contest['review_id']]
        resolution['basis_artifact_hashes']=[self.env['record']['artifacts'][0]['sha256']['value']]
        resolution['review_id']=review_key(resolution); record_review(resolution,self.store,local_actor='local-reviewer')
        self.assertEqual('CHECK_PASSED',project(query,self.store)['axes']['protocol']['status'])
        self.assertEqual(1,len(project(query,self.store)['coverage']['fulfilled']))

    def test_review_actor_and_parent_fail_closed(self):
        self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer')
        event=self.review(policy,'CONTEST')
        with self.assertRaisesRegex(CoverageError,'REVIEW_ACTOR_UNAUTHORIZED'): record_review(event,self.store,local_actor='other')
        event['supersedes_review_ids']=['ncov:review:2:'+'f'*64]; event['review_id']=review_key(event)
        with self.assertRaisesRegex(CoverageError,'REVIEW_PARENT_INVALID'): record_review(event,self.store,local_actor='local-reviewer')

    def test_annotation_cannot_supersede_contest(self):
        self.publish(); policy=self.policy(); register_policy(policy,self.store,local_actor='local-reviewer')
        contest=self.review(policy,'CONTEST'); record_review(contest,self.store,local_actor='local-reviewer')
        annotation=self.review(policy,'ADD_LIMITATION'); annotation['supersedes_review_ids']=[contest['review_id']]; annotation['review_id']=review_key(annotation)
        with self.assertRaisesRegex(CoverageError,'REVIEW_ANNOTATION_INVALID'): record_review(annotation,self.store,local_actor='local-reviewer')

    def test_contradictory_policy_is_refused(self):
        policy=self.policy(); policy['rules'].append({'obligation_id':'evd:protocol','axis':'protocol','predicate':'STAGE_AT_LEAST','operand':'model_attempts','expected':'-1'})
        policy['policy_sha256']=excluding_hash(policy,'policy_sha256')
        with self.assertRaisesRegex(CoverageError,'POLICY_RULE_INVALID'): register_policy(policy,self.store,local_actor='local-reviewer')

    def test_snapshot_pagination_does_not_mix_new_imports(self):
        self.publish()
        failed=copy.deepcopy(self.env); failed['record']['observed_result']='failure'; failed['record']['outcomes']['protocol']['status']='FAIL'; failed['record']['evidence_id']=evidence_key(failed['record']); self.publish(failed)
        query=query_for(self.env); query['page_limit']=1
        first=project(query,self.store); self.assertIsNotNone(first['next_cursor'])
        third=copy.deepcopy(self.env); third['record']['limitations'].append('third'); third['record']['evidence_id']=evidence_key(third['record']); self.publish(third)
        query['cursor']=first['next_cursor']; second=project(query,self.store)
        self.assertEqual(first['snapshot_id'],second['snapshot_id']); self.assertIsNone(second['next_cursor'])
        query['lane']='warp'
        with self.assertRaisesRegex(CoverageError,'CURSOR_MISMATCH'): project(query,self.store)

    def test_future_record_excluded(self):
        self.env['record']['source']['reported_at']=fact('2099-01-01T00:00:00Z'); self.env['record']['evidence_id']=evidence_key(self.env['record']); self.publish()
        query=query_for(self.env); query['as_of_utc']='2026-09-26T00:00:00Z'
        result=project(query,self.store); self.assertEqual('UNKNOWN',result['axes']['protocol']['status']); self.assertIn('FUTURE_REPORT',result['excluded'][0]['reason_codes'])

    def test_store_busy_is_infrastructure(self):
        other=sqlite3.connect(self.store.root/'coverage.sqlite3'); other.execute('BEGIN IMMEDIATE')
        try: result=self.publish()
        finally: other.rollback(); other.close()
        self.assertEqual('IMPORT_FAILED',result['status']); self.assertEqual(['STORE_BUSY'],result['reason_codes'])

    def test_unknown_store_version_not_downgraded(self):
        root=self.root/'future'; root.mkdir(); connection=sqlite3.connect(root/'coverage.sqlite3')
        connection.execute('CREATE TABLE store_meta(key TEXT PRIMARY KEY,value TEXT)'); connection.execute("INSERT INTO store_meta VALUES('version','99')"); connection.commit(); connection.close()
        with self.assertRaisesRegex(CoverageError,'STORE_VERSION_UNSUPPORTED'): Store(root)

    def test_trace_manifest_preserves_inconclusive_and_diagnostic_id(self):
        log=b'Retained diagnostic observations only'; harness=canonical({'timing':{'qpc_frequency':1000}})
        (self.root/'raw-neurotic.log').write_bytes(log); (self.root/'harness-manifest.json').write_bytes(harness)
        raw=canonical({'schema_version':1,'phase':'A observation only','session_id':'a'*32,'qpc_frequency':1000,'event_count':1,'counts':{'native-entry':1},'result':'Inconclusive','log_sha256':digest(log),'harness_manifest_sha256':digest(harness),'issues':['Event sequence has gaps'],'limitations':['No GPU completion proof']})
        result=import_trace(raw,self.root,self.store)
        self.assertEqual('PUBLISHED',result['status']); self.assertEqual('REPORTED',result['intake_class'])
        record=strict_load(self.store.db.execute('SELECT record_json FROM evidence').fetchone()[0])
        self.assertEqual('inconclusive',record['observed_result']); self.assertEqual('unknown',record['lane']); self.assertEqual('partial',record['trace_integrity']['status'])
        self.assertTrue(all(v['status']=='NOT_RUN' for v in record['outcomes'].values()))

    def test_inventory_69_claims_dry_run_and_repeat(self):
        raw=(Path(__file__).parent/'fixtures'/'MIGRATION_INVENTORY.json').read_bytes()
        dry=migrate_claims(raw); self.assertEqual(69,dry['record_count']); self.assertEqual(0,self.store.count('evidence'))
        first=migrate_claims(raw,self.store,publish=True); second=migrate_claims(raw,self.store,publish=True)
        self.assertEqual(69,self.store.count('evidence')); self.assertEqual(69,self.store.count('migrations'))
        self.assertTrue(all(r['result']['status']=='PUBLISHED' for r in first['records']))
        self.assertTrue(all(r['result']['idempotent'] for r in second['records']))
        self.assertEqual(dry['original_ids'],first['original_ids']); self.assertEqual('migration_inventory',first['source_kind'])
        self.assertEqual(0,first['discarded_fields'])

    def test_lab_cases_call_real_owner(self):
        for case in ('EVD001','EVD002','EVD004','EVD006'):
            result=run_lab_case(case,self.root/case)
            self.assertTrue(result['success'],result)

if __name__=='__main__': unittest.main()
