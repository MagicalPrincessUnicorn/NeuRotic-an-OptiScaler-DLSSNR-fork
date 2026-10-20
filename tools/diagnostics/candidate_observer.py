"""OBS-CLOSURE-1 offline metadata analysis. Standard library only; no rendering authority.

NeuRotic / EspiOwnage. Original Chat G schema and synthetic design fixtures are
preserved beside this file. Schema checks are supplemented by domain checks.
"""
import argparse
import hashlib
import json
import math
import re
import sys
import zipfile
from pathlib import Path

class Invalid(ValueError): pass
U64=(1<<64)-1
ASSETS=Path(__file__).with_suffix('')
SCHEMA=json.loads((ASSETS/'candidate_record.schema.json').read_text(encoding='utf-8'))
PROFILE='OBS-CLOSURE-1'
LIMITS={'event_bytes':512,'event_slots':4096,'reserved_control_slots':64,'session_events':65536,'data_events':65504,'control_events':32,
        'worker_batch':64,'window_ms':16,'window_data':512,'window_control':32,'resource_window':8,'frame_data':512,'resource_frame':8,
        'frame_streams':16,'frames_per_stream':4,'candidate_records':1024,'candidate_bytes':2048,'address_entries':1024,'tombstones':256,
        'window_quota_entries':1024,'frame_quota_bytes':1048576,'snapshot_bytes':65536,'snapshot_count':2,'display_candidates':64,
        'scratch_bytes':2097152,'memory_bytes':8388608,'duration_ms':30000,'trace_bytes':33554432,'footer_reserve':65536,
        'line_bytes':8192,'input_bytes':67108864,'input_events':65536,'report_bytes':4194304,'pack_bytes':67108864}
KINDS={'create_committed','create_placed','create_failed','create_capability_only',
       'create_anomalous_output','create_result_unclassified','create_interface_unclassified',
       'create_alternate_path','coverage','epoch_boundary','session_end'}
BUDGET_REASONS=('session_data','session_control','window_data','window_control','window_quota_slots',
                'resource_window','frame_streams','frame_order','frame_quota_slots','resource_frame','frame_data')

def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True,allow_nan=False).encode('utf-8')
def strict_json(text):
    def pairs(values):
        out={}
        for k,v in values:
            if k in out:raise Invalid('duplicate_json_key')
            out[k]=v
        return out
    try:
        if isinstance(text,bytes):text=text.decode('utf-8',errors='strict')
        def finite(value):
            number=float(value)
            if not math.isfinite(number):raise Invalid('nonfinite_number')
            return number
        return json.loads(text,object_pairs_hook=pairs,parse_float=finite,parse_constant=lambda _:(_ for _ in ()).throw(Invalid('nonfinite_number')))
    except (ValueError,RecursionError,UnicodeError) as exc:raise Invalid(str(exc)) from exc
def refuse_links(path):
    for part in (path.absolute(),*path.absolute().parents):
        if part.is_symlink() or part.is_junction():raise Invalid('reparse_path')
def u64(value,positive=False):
    if not isinstance(value,str) or not re.fullmatch(r'0|[1-9][0-9]{0,19}',value):raise Invalid('u64_encoding')
    n=int(value)
    if n>U64 or (positive and not n):raise Invalid('u64_range')
    return n
def uint(value,bits=32):
    if type(value)!=int or not 0<=value<(1<<bits):raise Invalid('unsigned_scalar_range')
    return value
def schema_check(value,schema,root=SCHEMA):
    """Validate the frozen local schema; refuse unsupported validation keywords."""
    if '$ref' in schema:
        target=root
        for part in schema['$ref'].removeprefix('#/').split('/'):
            target=target[part.replace('~1','/').replace('~0','~')]
        schema_check(value,target,root);return
    for key in ('oneOf','anyOf'):
        if key in schema:
            successes=0
            for option in schema[key]:
                try:schema_check(value,option,root);successes+=1
                except Invalid:pass
            if (key=='oneOf' and successes!=1) or (key=='anyOf' and not successes):raise Invalid('schema_alternative')
    if 'allOf' in schema:
        for option in schema['allOf']:schema_check(value,option,root)
    if 'const' in schema and (type(value)!=type(schema['const']) or value!=schema['const']):raise Invalid('schema_const')
    if 'enum' in schema and not any(type(value)==type(v) and value==v for v in schema['enum']):raise Invalid('schema_enum')
    types={'object':dict,'array':list,'string':str,'integer':int,'boolean':bool,'null':type(None),'number':(int,float)}
    if 'type' in schema:
        names=schema['type'] if isinstance(schema['type'],list) else [schema['type']]
        if not any(isinstance(value,types[n]) and not (n in ('integer','number') and isinstance(value,bool)) for n in names):raise Invalid('schema_type')
    if isinstance(value,dict):
        if any(k not in value for k in schema.get('required',[])):raise Invalid('schema_required')
        properties=schema.get('properties',{})
        if schema.get('additionalProperties') is False and set(value)-set(properties):raise Invalid('schema_extra_field')
        for k,v in value.items():
            if k in properties:schema_check(v,properties[k],root)
    if isinstance(value,list):
        if len(value)<schema.get('minItems',0) or len(value)>schema.get('maxItems',1<<30):raise Invalid('schema_array_bound')
        if schema.get('uniqueItems') and len({canonical(v) for v in value})!=len(value):raise Invalid('schema_unique')
        if 'items' in schema:
            for item in value:schema_check(item,schema['items'],root)
    if isinstance(value,str):
        if len(value)<schema.get('minLength',0) or len(value)>schema.get('maxLength',1<<30):raise Invalid('schema_string_bound')
        if 'pattern' in schema and not re.search(schema['pattern'],value):raise Invalid('schema_pattern')
    if type(value) in (int,float) and (value<schema.get('minimum',float('-inf')) or value>schema.get('maximum',float('inf'))):raise Invalid('schema_number_bound')

def support(descriptor):
    fmt=descriptor['format'];flags=descriptor['resource_flags']
    return (fmt['state']=='known' and fmt['value'] in (20,40,45,55)) or (flags['state']=='known' and bool(flags['value']&2))
def known(value):return {'state':'known','value':value}
def unknown(reason):return {'state':'unknown','reason':reason}
def factor(name,role,outcome,sequence,explanation,strength='decisive'):
    return {'factor_id':name,'role':role,'strength':strength,'outcome':outcome,
            'evidence_sequences':[str(sequence)],'explanation_code':explanation}
def expected_factors(d,seq):
    ledger=[]
    if support(d):ledger.append(factor('OBS-F-DEPTH-FLAG','support','supports',seq,'depth_capable_not_main_view_proof','strong'))
    for n,name in [('depth_or_array_size','OBS-F-DEPTH-ARRAY-PRIOR'),('mip_levels','OBS-F-DEPTH-MIP-PRIOR')]:
        if d[n]['state']=='known' and d[n]['value']>1:ledger.append(factor(name,'caution','cautions',seq,'descriptor_structure_only','weak'))
    for name,why in [('OBS-F-LIFETIME','create_only_no_lifetime'),('OBS-F-ID-FRAME','no_owner_frame'),('OBS-F-PRODUCER-EXACT','no_semantic_producer_evidence')]:
        ledger.append(factor(name,'hard_gate','unknown',seq,why))
    return ledger

def validate_candidate(c):
    schema_check(c,SCHEMA)
    key=c['candidate_key'];seq=u64(key['creation_sequence'],True);u64(key['birth_epoch'],True)
    if not re.fullmatch('[0-9a-f]{32}',key['session']):raise Invalid('session')
    if key['api']!=c['api'] or c['api']!=c['descriptor']['api']:raise Invalid('api_identity')
    if u64(c['observation_incarnation'],True)!=seq or u64(c['observation_source']['creation_sequence'],True)!=seq:raise Invalid('occurrence_identity')
    d=c['descriptor']
    for field in ('alignment','width'):
        if d[field]['state']=='known':u64(d[field]['value'])
    for field in ('depth_or_array_size','mip_levels'):
        if d[field]['state']=='known':uint(d[field]['value'],16)
    fp=c['descriptor_fingerprint']
    digest=hashlib.sha256(canonical({'descriptor_schema':'2.0','descriptor':d})).hexdigest()
    if fp['state']!='known' or fp['value']!=digest:raise Invalid('descriptor_hash')
    for gen,ref in [('owner_resource_generation','resource_ref'),('owner_device_generation','device_ref'),
                    ('owner_heap_generation','heap_ref'),('owner_content_version','content_ref')]:
        if c[gen]['state']=='known':
            u64(c[gen]['value'])
            if c[ref]['state']!='known':raise Invalid('owner_generation_without_evidence')
    temporal=c['temporal']
    if u64(temporal['first_sequence'],True)!=seq or u64(temporal['last_sequence'],True)<seq:raise Invalid('temporal_identity')
    for name in ('first_window','last_window','observation_count'):u64(temporal[name],True)
    for name in ('first_elapsed_ms','last_elapsed_ms'):
        if temporal[name]['state']!='known':raise Invalid('unsupported_clock')
        u64(temporal[name]['value'])
    if temporal['last_sequence']!=temporal['first_sequence'] or temporal['last_window']!=temporal['first_window'] or temporal['last_elapsed_ms']!=temporal['first_elapsed_ms']:raise Invalid('unsupported_use_event')
    if temporal['time_basis']!='worker_published_collection_clock':raise Invalid('clock_basis')
    offset=c['observation_source']['heap_offset']
    if c['observation_source']['callsite']=='D3D12_PLACED_NORMAL':
        if offset['state']!='known':raise Invalid('placed_offset')
        u64(offset['value'])
    elif offset!={'state':'not_applicable','reason':'committed_implicit_heap'}:raise Invalid('committed_offset')
    u64(c['correlation']['token'],True);u64(c['coverage']['drop_count'])
    coverage=c['coverage']
    if coverage['complete_transport'] and (coverage['loss_observed'] or u64(coverage['drop_count']) or not coverage['drop_count_exact'] or 'missing_footer' in coverage['limitations']):raise Invalid('complete_candidate_loss')
    previous=c['correlation']['previous_candidate']
    if previous:
        if previous['session']!=key['session'] or previous['api']!=key['api'] or u64(previous['birth_epoch'],True)>u64(key['birth_epoch']) or u64(previous['creation_sequence'],True)>=seq:raise Invalid('predecessor_identity')
        if c['correlation']['prior_address_history']!='reappearance':raise Invalid('predecessor_history')
    elif c['correlation']['prior_address_history']=='reappearance':raise Invalid('missing_predecessor')
    supported=bool(support(d));ids=[f['factor_id'] for f in c['factor_ledger']]
    if len(ids)!=len(set(ids)):raise Invalid('duplicate_factor')
    if ('OBS-F-DEPTH-FLAG' in ids)!=supported:raise Invalid('depth_factor')
    if c['ranking']['tier']!=('candidate_low' if supported else 'unranked'):raise Invalid('ranking_ceiling')
    if c['local_stage']!=('CANDIDATE' if supported else 'OBSERVED'):raise Invalid('stage_ceiling')
    if len(c['hypotheses'])!=int(supported):raise Invalid('unsupported_classifier')
    if c['factor_ledger']!=expected_factors(d,seq):raise Invalid('factor_semantics')
    expected_hypotheses=[{'signal_kind':'hardware_depth','method':'descriptor_factor_ledger','version':'create-only-1','verification':'unqualified','evidence_sequences':[str(seq)]}] if supported else []
    if c['hypotheses']!=expected_hypotheses:raise Invalid('hypothesis_semantics')
    # Current profile has no owner adapters; accepting free-form references would
    # claim evidence this implementation cannot lawfully supply or validate.
    for n in ('resource_ref','device_ref','heap_ref','content_ref','frame_ref','view_ref'):
        if c[n]!=unknown('owner_evidence_not_supplied'):raise Invalid('unsupported_owner_ref')
    for n in ('owner_resource_generation','owner_device_generation','owner_heap_generation','owner_content_version'):
        if c[n]!=unknown('owner_generation_not_supplied'):raise Invalid('unsupported_owner_generation')
    if c['lifetime']!=unknown('creation_is_not_liveness'):raise Invalid('unsupported_lifetime')
    if temporal['update_cadence']!=unknown('no_owner_use_events') or temporal['content_stability']!=unknown('no_content_observation'):raise Invalid('unsupported_temporal_evidence')
    if temporal['frame_budget_basis']!='unknown' or temporal['observation_count']!='1':raise Invalid('unsupported_frame_or_use_count')
    for value in d.values():
        if isinstance(value,dict) and value['state']!='known' and value!=unknown('descriptor_not_observed'):raise Invalid('descriptor_reason')
    if c['retention']['reason'] not in ('within_observation_window','observation_epoch_or_age','later_creation_same_retained_address'):raise Invalid('retention_reason')
    if set(c['coverage']['limitations'])-{'resource_destruction_not_observed','aliasing_not_observed','main_view_not_established','missing_footer'}:raise Invalid('coverage_reason')
    if len(canonical(c))>8192:raise Invalid('candidate_line_limit')

EVENT_FIELDS={'type','schema_version','sequence','epoch','window','elapsed_ms','kind','descriptor',
              'descriptor_known','result','token','predecessor','source','heap_offset',
              'native_observation','present_observation','coverage_mask','iid','heap','reason'}
def validate_event(e):
    if not isinstance(e,dict) or set(e)-EVENT_FIELDS:raise Invalid('event_extra_field')
    if e.get('type')!='event' or e.get('schema_version')!='2.0' or e.get('kind') not in KINDS:raise Invalid('event_profile')
    for n in ('sequence','epoch','window'):u64(e[n],True)
    for n in ('elapsed_ms','native_observation','present_observation','token','predecessor'):u64(e[n])
    uint(e['result']);uint(e['coverage_mask'])
    if 'reason' in e and uint(e['reason'])>11:raise Invalid('reason')
    if 'heap' in e:
        heap=e['heap'];names={'known','type','cpu_page','memory_pool','creation_node','visible_node','flags'}
        if not isinstance(heap,dict) or set(heap)!=names or type(heap['known'])!=bool:raise Invalid('heap_metadata')
        for n in names-{'known'}:uint(heap[n])
    if type(e['descriptor_known'])!=bool:raise Invalid('descriptor_known')
    schema_check(e['descriptor'],SCHEMA['$defs']['descriptor'])
    if any((v['state']=='known')!=e['descriptor_known'] for k,v in e['descriptor'].items() if k!='api'):raise Invalid('descriptor_availability')
    if e['heap_offset'] is not None:u64(e['heap_offset'])
    if e['source'] not in ('D3D12_COMMITTED_NORMAL','D3D12_PLACED_NORMAL','D3D12_IGD_ALTERNATE','OBSERVER_CONTROL'):raise Invalid('source')
    if 'iid' in e and not re.fullmatch('[0-9a-f]{32}',e['iid']):raise Invalid('iid')
    if e['kind'] in ('create_committed','create_placed'):
        if e['result']!=0 or u64(e['token'])==0:raise Invalid('ineligible_creation')
        expected='D3D12_COMMITTED_NORMAL' if e['kind']=='create_committed' else 'D3D12_PLACED_NORMAL'
        if e['source']!=expected or (e['kind']=='create_placed')!=(e['heap_offset'] is not None):raise Invalid('creation_source')
    elif u64(e['token']) or u64(e['predecessor']):raise Invalid('noncreation_identity')
    if (e['kind']=='create_alternate_path')!=(e['source']=='D3D12_IGD_ALTERNATE'):raise Invalid('alternate_source')
    if (e['kind'] in ('coverage','epoch_boundary','session_end'))!=(e['source']=='OBSERVER_CONTROL'):raise Invalid('control_source')
    if e['kind']=='create_failed' and not e['result']&0x80000000:raise Invalid('failed_result')
    for name in ('alignment','width'):
        if e['descriptor'][name]['state']=='known':u64(e['descriptor'][name]['value'])
    for name in ('depth_or_array_size','mip_levels'):
        if e['descriptor'][name]['state']=='known':uint(e['descriptor'][name]['value'],16)

def candidate(e,session,complete):
    seq=e['sequence'];d=e['descriptor'];supported=bool(support(d));key={'session':session,'birth_epoch':e['epoch'],'api':'D3D12','creation_sequence':seq}
    c={'schema_version':'2.0','profile_id':PROFILE,'candidate_key':key,'observation_incarnation':seq,
       'api':'D3D12','descriptor':d,'descriptor_fingerprint':known(hashlib.sha256(canonical({'descriptor_schema':'2.0','descriptor':d})).hexdigest()),
       'local_stage':'CANDIDATE' if supported else 'OBSERVED','admission_authority':'none','may_admit':False,
       'external_qualification':'unavailable','lifetime':unknown('creation_is_not_liveness'),
       'correlation':{'token':e['token'],'previous_candidate':None,'prior_address_history':'not_seen_in_retained_map','meaning':'observation_only_not_resource_identity'},
       'observation_source':{'seam_id':'S03','callsite':e['source'],'outcome':'object_observed','descriptor_basis':'call_arguments',
            'creation_sequence':seq,'heap_offset':known(e['heap_offset']) if e['heap_offset'] is not None else {'state':'not_applicable','reason':'committed_implicit_heap'}},
       'coverage':{'lifecycle_coverage':'create_only','creation_family':'installed','complete_transport':complete,
            'loss_observed':False,'drop_count':'0','drop_count_exact':True,
            'limitations':['resource_destruction_not_observed','aliasing_not_observed','main_view_not_established']},
       'temporal':{'first_sequence':seq,'last_sequence':seq,'first_window':e['window'],'last_window':e['window'],
            'first_elapsed_ms':known(e['elapsed_ms']),'last_elapsed_ms':known(e['elapsed_ms']),
            'time_basis':'worker_published_collection_clock','observation_count':'1','frame_budget_basis':'unknown',
            'update_cadence':unknown('no_owner_use_events'),'content_stability':unknown('no_content_observation')},
       'retention':{'state':'RETAINED','reason':'within_observation_window'},'hypotheses':[],
       'factor_ledger':[],'ranking':{'model':'OBS-RANK-001','profile':'create-only-1','tier':'candidate_low' if supported else 'unranked','score_as_probability':False}}
    for n in ('resource_ref','device_ref','heap_ref','content_ref','frame_ref','view_ref'):c[n]=unknown('owner_evidence_not_supplied')
    for n in ('owner_resource_generation','owner_device_generation','owner_heap_generation','owner_content_version'):c[n]=unknown('owner_generation_not_supplied')
    if supported:
        c['hypotheses']=[{'signal_kind':'hardware_depth','method':'descriptor_factor_ledger','version':'create-only-1',
                         'verification':'unqualified','evidence_sequences':[seq]}]
        c['factor_ledger'].append(factor('OBS-F-DEPTH-FLAG','support','supports',seq,'depth_capable_not_main_view_proof','strong'))
    c['factor_ledger']=expected_factors(d,seq)
    validate_candidate(c);return c

def reduce_events(events,session,complete):
    if not re.fullmatch('[0-9a-f]{32}',session):raise Invalid('session')
    seen={};records={};last=0;clock=0;epoch=0;window=0;tokens=set();stats={'duplicates':0,'events':0,'evicted':0,'omitted':0,'sequence_gaps':0}
    for e in events:
        validate_event(e);seq=u64(e['sequence'],True);encoded=canonical(e)
        if seq in seen:
            if seen[seq]!=hashlib.sha256(encoded).digest():raise Invalid('conflicting_event_id')
            stats['duplicates']+=1;continue
        if seq<=last:raise Invalid('out_of_order')
        if seq!=last+1:stats['sequence_gaps']+=1
        seen[seq]=hashlib.sha256(encoded).digest();last=seq;stats['events']+=1
        now=u64(e['elapsed_ms']);current_epoch=u64(e['epoch'],True)
        current_window=u64(e['window'],True)
        if now<clock or current_epoch<epoch or current_window<window:raise Invalid('regressing_observation_clock_or_epoch')
        clock=now;epoch=current_epoch;window=current_window
        expired=[sid for sid,r in records.items() if now-u64(r['temporal']['last_elapsed_ms']['value'])>=10000]
        for sid in expired:del records[sid];stats['evicted']+=1
        for prior in records.values():
            age=u64(e['elapsed_ms'])-u64(prior['temporal']['last_elapsed_ms']['value'])
            if (prior['candidate_key']['birth_epoch']!=e['epoch'] or age>=2000) and prior['retention']['state']=='RETAINED':
                prior['retention']={'state':'STALE','reason':'observation_epoch_or_age'}
        if e['kind'] not in ('create_committed','create_placed'):continue
        token=u64(e['token'],True)
        if token in tokens:raise Invalid('redaction_token_reuse')
        tokens.add(token);c=candidate(e,session,complete);pre=u64(e['predecessor'])
        if pre:
            if pre>=seq:raise Invalid('predecessor_order')
            if pre in records:
                c['correlation']['previous_candidate']=records[pre]['candidate_key'].copy()
                c['correlation']['prior_address_history']='reappearance'
                records[pre]['retention']={'state':'SUPERSEDED_FOR_CORRELATION','reason':'later_creation_same_retained_address'}
            else:c['correlation']['prior_address_history']='unavailable'
        if len(records)>=1024:
            def victim(pair):
                sid,r=pair;age=u64(e['elapsed_ms'])-u64(r['temporal']['last_elapsed_ms']['value'])
                state=r['retention']['state'];return (0 if age>=10000 else 1 if state=='SUPERSEDED_FOR_CORRELATION' else 2 if state=='STALE' else 3,sid)
            remove=min(records.items(),key=victim)[0];del records[remove];stats['evicted']+=1
        records[seq]=c
    # Public order is separate from reduction/enqueue order.
    ordered=sorted(records.values(),key=lambda c:(not bool(c['hypotheses']),sum(f['role']=='caution' for f in c['factor_ledger']),-int(c['observation_incarnation']),canonical(c['candidate_key'])))
    for c in ordered:
        if stats['sequence_gaps']:c['coverage']['complete_transport']=False
        validate_candidate(c)
    return ordered,stats

def validate_budget_refusals(value, *, dropped=None, drop_exact=None, loss_mask=None, complete=False):
    if not isinstance(value,dict) or set(value)!={'profile','reasons'} or value['profile']!='quota-reasons-1':raise Invalid('budget_diagnostics_profile')
    reasons=value['reasons']
    if not isinstance(reasons,list) or len(reasons)!=len(BUDGET_REASONS):raise Invalid('budget_diagnostics_reasons')
    total=0;exact=True
    for name,row in zip(BUDGET_REASONS,reasons):
        if not isinstance(row,dict) or set(row)!={'reason','count','exact'} or row['reason']!=name or type(row['exact'])!=bool:raise Invalid('budget_diagnostics_shape')
        total+=u64(row['count']);exact=exact and row['exact']
    if complete and (total or not exact):raise Invalid('budget_diagnostics_complete')
    if dropped is not None and drop_exact and total>dropped:raise Invalid('budget_diagnostics_drop_count')
    if loss_mask is not None:
        if (total or not exact) and not loss_mask&8:raise Invalid('budget_diagnostics_loss_mask')
        if loss_mask&8 and exact and total==0:raise Invalid('budget_diagnostics_missing_loss')
        if dropped is not None and drop_exact:
            if loss_mask==8 and exact and total!=dropped:raise Invalid('budget_diagnostics_drop_count')
    return total

def read_trace(path):
    refuse_links(path)
    if not path.is_file() or path.is_symlink() or path.stat().st_size>64*1024*1024:raise Invalid('input_limit_or_path')
    header=None;footer=None;events=[];total=0;digest=hashlib.sha256()
    with path.open('rb') as f:
        while True:
            line=f.readline(8193)
            if not line:break
            total+=len(line);digest.update(line)
            if total>64*1024*1024:raise Invalid('input_limit')
            if len(line)>8192:raise Invalid('line_limit')
            if not line.endswith(b'\n'):raise Invalid('incomplete_record')
            value=strict_json(line)
            if not isinstance(value,dict):raise Invalid('record_shape')
            kind=value.get('type')
            if header is None:
                allowed={'type','schema_version','profile_id','session','redaction','source_identity','protection_policy','limits','family_states','capture_start_offset','owner_facts'}
                if kind!='header' or set(value)-allowed or value.get('schema_version')!='2.0' or value.get('profile_id')!=PROFILE:raise Invalid('header_profile')
                if value.get('redaction')!='session_local_observation_tokens':raise Invalid('redaction_profile')
                if not re.fullmatch('[0-9a-f]{32}',value.get('session','')):raise Invalid('session')
                if value.get('source_identity')!='unavailable' or value.get('protection_policy')!='unknown':raise Invalid('unsupported_header_evidence')
                limits=value.get('limits')
                if limits!=LIMITS:raise Invalid('budget_profile')
                if 'family_states' in value:
                    mask=uint(value['family_states'])
                    if mask&~0x33 or (mask&15)>3 or ((mask>>4)&15)>3:raise Invalid('family_states')
                if 'capture_start_offset' in value and value['capture_start_offset']!='unavailable':raise Invalid('start_offset')
                if 'owner_facts' in value and value['owner_facts']!='unsupported':raise Invalid('owner_profile')
                header=value
            elif kind=='event' and footer is None:
                if len(events)>=65536:raise Invalid('event_limit')
                events.append(value)
            elif kind=='footer' and footer is None:
                allowed={'type','schema_version','last_sequence','enqueued','serialized','dropped','drop_exact','loss_mask','high_water','reason','complete_transport','elapsed_ms','bytes_before_footer','families','budget_refusals'}
                if set(value)-allowed or value.get('schema_version')!='2.0':raise Invalid('footer_profile')
                for n in ('last_sequence','enqueued','serialized','dropped'):u64(value[n])
                for n in ('drop_exact','complete_transport'):
                    if type(value[n])!=bool:raise Invalid('footer_boolean')
                for n in ('loss_mask','high_water','reason'):uint(value[n])
                if value['high_water']>4096 or value['reason']>11:raise Invalid('footer_domains')
                if 'elapsed_ms' in value:u64(value['elapsed_ms'])
                if 'bytes_before_footer' in value and u64(value['bytes_before_footer'])!=total-len(line):raise Invalid('footer_bytes')
                if 'families' in value:
                    if not isinstance(value['families'],list) or len(value['families'])!=4:raise Invalid('footer_families')
                    for family in value['families']:
                        if set(family)!={'enqueued','dropped','drop_exact'} or type(family['drop_exact'])!=bool:raise Invalid('footer_family_shape')
                        u64(family['enqueued']);u64(family['dropped'])
                    if sum(u64(f['enqueued']) for f in value['families'])!=u64(value['enqueued']):raise Invalid('family_enqueued_count')
                    if value['drop_exact'] and all(f['drop_exact'] for f in value['families']) and sum(u64(f['dropped']) for f in value['families'])!=u64(value['dropped']):raise Invalid('family_drop_count')
                if 'budget_refusals' in value:
                    validate_budget_refusals(value['budget_refusals'],dropped=u64(value['dropped']),drop_exact=value['drop_exact'],
                                            loss_mask=value['loss_mask'],complete=value['complete_transport'])
                footer=value
            else:raise Invalid('trace_order')
    if header is None:raise Invalid('missing_header')
    return header,events,footer,digest.hexdigest()

def report_text(records,report):
    explanations=['NeuRotic passive discovery — observation evidence only',f"Candidates: {len(records)}; complete transport: {report['complete_transport']}",
                  'No candidate grants resource access or model admission.']
    if 'budget_refusals' in report:
        for row in report['budget_refusals']['reasons']:
            if u64(row['count']) or not row['exact']:
                explanations.append(f"Budget refusal {row['reason']}: {row['count']} ({'exact' if row['exact'] else 'lower bound'})")
    for c in records[:64]:explanations.append(f"Occurrence {c['observation_incarnation']}: {c['ranking']['tier']}; {c['retention']['state']}; lifetime/frame/content unknown")
    return ('\n'.join(explanations)+'\n').encode('utf-8')
def validate_report(report,records):
    required={'schema_version','profile_id','session','complete_transport','graphics_coverage','may_admit','candidate_count','duplicates','events','evicted','omitted',
              'sequence_gaps','limitations','family_coverage','owner_facts','trace_sha256'}
    if not isinstance(report,dict) or set(report)-required-{'family_transport','budget_refusals'} or required-set(report):raise Invalid('report_shape')
    if report['schema_version']!='2.0' or report['profile_id']!=PROFILE or report['graphics_coverage']!='create_only' or report['may_admit'] is not False or report['owner_facts']!='unsupported':raise Invalid('report_authority')
    if type(report['complete_transport'])!=bool or not re.fullmatch('[0-9a-f]{32}',report['session']) or not re.fullmatch('[0-9a-f]{64}',report['trace_sha256']):raise Invalid('report_identity')
    for n in ('candidate_count','duplicates','events','evicted','omitted','sequence_gaps'):uint(report[n])
    if report['candidate_count']!=len(records) or len(records)>1024:raise Invalid('report_count')
    if report['limitations']!=['creation_is_not_liveness','no_owner_frame_or_content','no_semantic_qualification'] and report['limitations']!=['creation_is_not_liveness','no_owner_frame_or_content','no_semantic_qualification','missing_footer']:raise Invalid('report_limitations')
    family=report['family_coverage']
    if not isinstance(family,dict) or set(family)!={'committed','placed'} or any(v not in ('unknown','installed','not_installed','failed') for v in family.values()):raise Invalid('report_families')
    if 'family_transport' in report:
        families=report['family_transport']
        if not isinstance(families,list) or len(families)!=4:raise Invalid('report_family_count')
        for f in families:
            if set(f)!={'enqueued','dropped','drop_exact'} or type(f['drop_exact'])!=bool:raise Invalid('report_family_shape')
            u64(f['enqueued']);u64(f['dropped'])
            if report['complete_transport'] and (not f['drop_exact'] or u64(f['dropped'])):raise Invalid('report_family_loss')
    if report['complete_transport'] and (report['sequence_gaps'] or 'missing_footer' in report['limitations']):raise Invalid('report_completeness')
    if 'budget_refusals' in report:
        families=report.get('family_transport')
        validate_budget_refusals(report['budget_refusals'],complete=report['complete_transport'],
                                dropped=sum(u64(f['dropped']) for f in families) if families else None,
                                drop_exact=all(f['drop_exact'] for f in families) if families else None)
    for c in records:
        if c['candidate_key']['session']!=report['session'] or c['coverage']['complete_transport']!=report['complete_transport']:raise Invalid('report_candidate_identity')
        if 'budget_refusals' in report:
            validate_budget_refusals(report['budget_refusals'],dropped=u64(c['coverage']['drop_count']),drop_exact=c['coverage']['drop_count_exact'])

def analyze(trace,out):
    trace=Path(trace);out=Path(out)
    refuse_links(out)
    if out.exists():raise Invalid('output_exists')
    header,events,footer,digest=read_trace(trace)
    records,stats=reduce_events(events,header['session'],False)
    complete=bool(footer and footer['complete_transport'] and footer['drop_exact'] and footer['loss_mask']==0 and
                  u64(footer['dropped'])==0 and not stats['sequence_gaps'] and u64(footer['serialized'])==u64(footer['enqueued'])==stats['events'] and
                  u64(footer['last_sequence'])==(u64(events[-1]['sequence']) if events else 0))
    if footer and 'families' in footer:complete=complete and all(f['drop_exact'] and not u64(f['dropped']) for f in footer['families'])
    if footer and 'elapsed_ms' in footer:
        terminal=u64(footer['elapsed_ms'])
        if events and terminal<u64(events[-1]['elapsed_ms']):raise Invalid('footer_clock')
        kept=[]
        for c in records:
            age=terminal-u64(c['temporal']['last_elapsed_ms']['value'])
            if age>=10000:stats['evicted']+=1
            else:
                if age>=2000 and c['retention']['state']=='RETAINED':c['retention']={'state':'STALE','reason':'observation_epoch_or_age'}
                kept.append(c)
        records=kept
    states={0:'unknown',1:'not_installed',2:'installed',3:'failed'}
    family={'committed':'unknown','placed':'unknown'}
    for e in events:
        if e['kind']=='coverage':
            mask=e['coverage_mask']
            if mask&~0x33 or (mask&15)>3 or ((mask>>4)&15)>3:raise Invalid('family_coverage')
            family={'committed':states[mask&15],'placed':states[(mask>>4)&15]}
    for c in records:c['coverage']['complete_transport']=complete
    if footer:
        for c in records:
            c['coverage'].update(loss_observed=bool(footer['loss_mask'] or int(footer['dropped'])),drop_count=footer['dropped'],drop_count_exact=footer['drop_exact'])
    else:
        for c in records:c['coverage']['limitations'].append('missing_footer')
    report={'schema_version':'2.0','profile_id':PROFILE,'session':header['session'],'complete_transport':complete,
            'graphics_coverage':'create_only','may_admit':False,'candidate_count':len(records),**stats,
            'limitations':['creation_is_not_liveness','no_owner_frame_or_content','no_semantic_qualification'],
            'family_coverage':family,'owner_facts':'unsupported','trace_sha256':digest}
    if footer and 'families' in footer:report['family_transport']=footer['families']
    if footer and 'budget_refusals' in footer:report['budget_refusals']=footer['budget_refusals']
    if not footer:report['limitations'].append('missing_footer')
    # Bound exports by count with explicit omissions; never truncate a JSON document.
    while len(canonical(records))>4*1024*1024:records.pop();report['omitted']+=1
    report['candidate_count']=len(records)
    for c in records:validate_candidate(c)
    validate_report(report,records)
    out.mkdir(parents=True)
    (out/'candidates.json').write_bytes(canonical(records)+b'\n')
    (out/'report.json').write_bytes(canonical(report)+b'\n')
    (out/'report.txt').write_bytes(report_text(records,report))
    inventory={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.iterdir())}
    (out/'inventory.json').write_bytes(canonical(inventory)+b'\n');return report

def pack(source,out):
    source=Path(source);out=Path(out)
    refuse_links(source);refuse_links(out)
    if out.exists() or not source.is_dir() or source.is_symlink():raise Invalid('pack_path')
    expected={'candidates.json','report.json','report.txt','inventory.json'}
    if {p.name for p in source.iterdir()}!=expected:raise Invalid('unexpected_pack_members')
    payloads={};total=0
    for p in source.iterdir():
        if p.is_symlink() or p.is_junction() or not p.is_file():raise Invalid('unsafe_pack_member')
        with p.open('rb') as f:payload=f.read(64*1024*1024+1-total)
        total+=len(payload)
        if total>64*1024*1024:raise Invalid('pack_limit')
        payloads[p.name]=payload
    inventory=strict_json(payloads['inventory.json'])
    if set(inventory)!=expected-{'inventory.json'}:raise Invalid('inventory_members')
    for name,payload in payloads.items():
        if name!='inventory.json':
            item=inventory[name]
            if set(item)!={'bytes','sha256'} or len(payload)!=item['bytes'] or hashlib.sha256(payload).hexdigest()!=item['sha256']:raise Invalid('pack_hash')
    records=strict_json(payloads['candidates.json'])
    if not isinstance(records,list) or len(records)>1024 or len(payloads['candidates.json'])>4*1024*1024:raise Invalid('candidate_export_limit')
    for c in records:validate_candidate(c)
    report=strict_json(payloads['report.json']);validate_report(report,records)
    if len(payloads['report.json'])>4*1024*1024 or payloads['report.txt'].replace(b'\r\n',b'\n')!=report_text(records,report):raise Invalid('report_text')
    with zipfile.ZipFile(out,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for name in sorted(expected):
            info=zipfile.ZipInfo(name,(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,payloads[name])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    subs=parser.add_subparsers(dest='command',required=True)
    a=subs.add_parser('analyze');a.add_argument('--trace',required=True,type=Path);a.add_argument('--out',required=True,type=Path)
    p=subs.add_parser('pack');p.add_argument('--input',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    try:
        if args.command=='analyze':print(canonical(analyze(args.trace,args.out)).decode())
        else:pack(args.input,args.out);print('PACKED')
        return 0
    except (Invalid,OSError,KeyError,TypeError) as exc:print(f'REFUSED: {exc}',file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
