"""Synthetic parser/contract controls; never production build evidence.

Breaks caught: dropping a consumed input, changing effective arguments, treating
an arbitrary generated header as produced, ambiguous SDK depfiles, and binding
the worker identity to a different forwarder.
"""
from pathlib import Path
import copy, hashlib, tempfile, unittest
import sys
sys.path.insert(0,str(Path(__file__).parents[1]))
sys.path.insert(0,str(Path(__file__).parents[2]/'packaging/customer'))
import ProducerCapture as c

def pin(p, kind='source'):
    b=p.read_bytes()
    return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),kind=kind)

class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='parser-fixture-',dir=Path(__file__).parent)
        self.root=Path(self.temp.name)
        self.src=self.root/'PrepareInput.hlsl';self.src.write_text('void CSMain() {}\n')
        self.tool=self.root/'dxc.exe';self.tool.write_bytes(b'INERT_TOOL_NOT_EXECUTED')
        self.out=self.root/'PrepareInput.h';self.out.write_text('synthetic generated header\n')
        self.pins=[pin(self.src),pin(self.tool,'tool')]
        self.plan=dict(id='worker-PrepareInput',kind='dxc-direct',configuration='Release|x64',
            cwd=str(self.root),command=[str(self.tool),'-T','cs_6_0','-Fh',str(self.out),str(self.src)],
            inputs=[str(self.src)],tools=[str(self.tool)],outputs=[str(self.out)],
            environment={'INCLUDE':'fixture'})
        self.record=dict(id=self.plan['id'],scope='FIXTURE_POLICY_ONLY',configuration='Release|x64',
            cwd=str(self.root),command=self.plan['command'][:],exit_code=0,
            inputs=[pin(self.src)],tools=[pin(self.tool,'tool')],outputs=[pin(self.out)],
            environment={'INCLUDE':'fixture'})
    def tearDown(self):self.temp.cleanup()
    def test_valid_direct_action(self):
        self.assertEqual(c.validate_action(self.plan,self.record,self.pins)[0]['path'],str(self.out))
    def test_missing_consumed_input_reaches_source_gate(self):
        with self.assertRaisesRegex(ValueError,'Unfrozen source.*PrepareInput.hlsl'):
            c.validate_action(self.plan,self.record,self.pins[1:])
    def test_effective_option_mismatch(self):
        self.record['command'][2]='cs_5_0'
        with self.assertRaisesRegex(ValueError,'Command differs'):
            c.validate_action(self.plan,self.record,self.pins)
    def test_output_mismatch(self):
        self.out.write_text('changed after producer')
        with self.assertRaisesRegex(ValueError,'Changed output'):
            c.validate_action(self.plan,self.record,self.pins)
    def test_unproven_generated_read(self):
        rogue=self.root/'rogue.h';rogue.write_text('unproven')
        self.plan['inputs'].append(str(rogue));self.record['inputs'].append(pin(rogue))
        with self.assertRaisesRegex(ValueError,'Unfrozen source.*rogue.h'):
            c.validate_action(self.plan,self.record,self.pins)
    def test_named_generated_edge(self):
        upstream=self.root/'forwarder.dll';upstream.write_text('producer output')
        self.plan['inputs'].append(str(upstream));self.record['inputs'].append(pin(upstream))
        self.assertEqual(len(c.validate_action(self.plan,self.record,self.pins,[pin(upstream)])),1)
    def test_declared_tool_is_required(self):
        with self.assertRaisesRegex(ValueError,'Unfrozen tool.*dxc.exe'):
            c.validate_action(self.plan,self.record,self.pins[:1])
    def test_environment_mismatch(self):
        self.record['environment']['INCLUDE']='changed'
        with self.assertRaisesRegex(ValueError,'Environment differs'):
            c.validate_action(self.plan,self.record,self.pins)
    def test_depfile_drive_continuation(self):
        actual=c.depfile_paths('C:/out/p.h: C:/src/p.hlsl \\\n C:/src/inc.h\n','C:/out/p.h','C:/work')
        self.assertEqual(actual,['C:/src/p.hlsl','C:/src/inc.h'])
    def test_depfile_wrong_output(self):
        with self.assertRaisesRegex(ValueError,'Depfile target differs'):
            c.depfile_paths('C:/out/other.h: C:/src/p.hlsl','C:/out/p.h','C:/work')
    def test_sdk_unescaped_space_refused(self):
        with self.assertRaisesRegex(ValueError,'Ambiguous SDK depfile'):
            c.depfile_paths('C:/out/p.h: C:/Program Files/input.h','C:/out/p.h','C:/work')
    def test_direct_shader_no_includes(self):
        self.assertEqual(c.direct_shader_inputs(self.src),[str(self.src)])
    def test_direct_shader_include_requires_new_capture(self):
        self.src.write_text('#include "unobserved.h"\n')
        with self.assertRaisesRegex(ValueError,'Shader external dependency'):
            c.direct_shader_inputs(self.src)
    def test_direct_shader_comment_split_include_refused(self):
        self.src.write_text('#/**/include "unobserved.h"\n')
        with self.assertRaisesRegex(ValueError,'Shader external dependency'):
            c.direct_shader_inputs(self.src)
    def test_permutation_exact_children(self):
        primary=self.root/'flow_permutations.h';child=self.root/('flow_'+'a'*32+'.h')
        primary.write_text('#include "'+child.name+'"\n');child.write_text('synthetic')
        rogue=self.root/'rogue.h';rogue.write_text('not included')
        self.assertEqual(c.permutation_outputs(primary,'flow'),[str(primary),str(child)])
    def test_permutation_escape_refused(self):
        primary=self.root/'flow_permutations.h';primary.write_text('#include "../rogue.h"\n')
        with self.assertRaisesRegex(ValueError,'Unmodeled permutation child'):
            c.permutation_outputs(primary,'flow')
    def test_identity_binds_actual_binary_bytes(self):
        binary=self.root/'forwarder.dll';binary.write_bytes(b'abc')
        self.assertEqual(c.identity_bytes(binary),b'#pragma once\ninline constexpr char WindowNrForwarderSha256[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";\n')
    def test_forwarder_native_parent_missing_link_input(self):
        output=self.root/'builds/parent';output.mkdir(parents=True)
        tracking=output/'forwarder-obj';tracking.mkdir()
        binary=output/'nvngx.dll_dlssnr.dll';binary.write_bytes(b'inert dll')
        lib=self.root/'d3d12.lib';lib.write_bytes(b'inert lib')
        tool=self.root/'link.exe';tool.write_bytes(b'inert tool')
        for family,text in [('read','^'+str(lib)+'\n'+str(lib)),('write','^'+str(lib)+'\n'+str(binary)),('command','^'+str(lib)+'\n/DLL /OUT:'+str(binary))]:
            (tracking/('link.'+family+'.1.tlog')).write_text(text,encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Consumed input absent.*d3d12.lib'):
            c.native_parent(self.root,output,[pin(tool,'tool')],tracking,binary)
    def test_forwarder_native_parent_exact_selected_output(self):
        output=self.root/'builds/parent';output.mkdir(parents=True)
        tracking=output/'forwarder-obj';tracking.mkdir()
        binary=output/'nvngx.dll_dlssnr.dll';binary.write_bytes(b'inert dll')
        lib=self.root/'d3d12.lib';lib.write_bytes(b'inert lib')
        tool=self.root/'link.exe';tool.write_bytes(b'inert tool')
        for family,text in [('read','^'+str(lib)+'\n'+str(lib)),('write','^'+str(lib)+'\n'+str(binary)),('command','^'+str(lib)+'\n/DLL /OUT:'+str(binary))]:
            (tracking/('link.'+family+'.1.tlog')).write_text(text,encoding='utf-16')
        actual=c.native_parent(self.root,output,[pin(tool,'tool'),pin(lib,'link')],tracking,binary)
        self.assertEqual(actual['selected_outputs'][0]['path'],str(binary))
        self.assertEqual(len(actual['producers']),1)
    def test_ffx_depfile_include_omission_hits_input_gate(self):
        primary=self.root/'flow_permutations.h';child=self.root/('flow_'+'a'*32+'.h')
        primary.write_text('#include "'+child.name+'"\n');child.write_text('synthetic')
        depfile=self.root/'flow_permutations.h.d';depfile.write_text('synthetic retained depfile')
        include=self.root/'required.h';include.write_text('required shader input')
        self.plan.update(kind='ffx-depfile',shader_name='flow',outputs=[str(primary),str(depfile)])
        self.record['outputs']=[pin(p) for p in (primary,depfile,child)]
        self.record['inputs'].append(pin(include))
        with self.assertRaisesRegex(ValueError,'Unfrozen source.*required.h'):
            c.validate_action(self.plan,self.record,self.pins)
    def test_cmake_generator_read_enters_required_closure(self):
        import json
        reply=self.root/'reply';reply.mkdir()
        source=self.root/'flow';source.mkdir()
        cmake=source/'CMakeLists.txt';cmake.write_text('synthetic')
        shader=source/'PreparedFlowConvert.hlsl';shader.write_text('synthetic')
        docs={'index-fixture.json':{'objects':[{'kind':'cmakeFiles','jsonFile':'files.json'},{'kind':'codemodel','jsonFile':'model.json'}]},
          'files.json':{'paths':{'source':str(source)},'inputs':[{'path':str(cmake)}]},
          'model.json':{'paths':{'source':str(source),'build':str(self.root)},'configurations':[{'targets':[{'id':'flow','name':'neurotic_prepared_flow','jsonFile':'target.json'}]}]},
          'target.json':{'sources':[{'path':str(self.src)}],'artifacts':[{'path':'neurotic_prepared_flow.lib'}]}}
        for name,doc in docs.items():(reply/name).write_text(json.dumps(doc))
        trace=self.root/'trace.jsonl';trace.write_text(json.dumps({'cmd':'file','file':str(cmake),'args':['READ',str(shader),'text']}))
        actual=c.cmake_inputs(reply,trace)
        self.assertIn({'path':str(shader),'kind':'generator-input'},actual['inputs'])


class TlogRecordControls(unittest.TestCase):
    """Inert UTF16 tracking fixtures; no compiler or module attestation.

    Catch source-root/write-value conflation without relaxing actual value,
    source-freeze, tool, generated-edge or selected-output requirements.
    """
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='tlog-record-fixture-',dir=Path(__file__).parent)
        self.root=Path(self.temp.name)
        self.output=self.root/'builds/owned-build';self.output.mkdir(parents=True)
        self.tracking=self.output/'forwarder-obj';self.tracking.mkdir()
        self.source=self.root/'shipping-source'/'forwarder.cpp';self.source.parent.mkdir();self.source.write_bytes(b'fixture source')
        self.header=self.root/'fixture.h';self.header.write_bytes(b'fixture include')
        self.library=self.root/'d3d12.lib';self.library.write_bytes(b'fixture library')
        self.obj=self.output/'forwarder.obj';self.obj.write_bytes(b'fixture object')
        self.binary=self.output/'nvngx.dll_dlssnr.dll';self.binary.write_bytes(b'fixture dll')
        self.pins=[pin(p) for p in (self.source,self.header,self.library)]
        for name in ('cl.exe','c1xx.dll','c2.dll','link.exe'):
            p=self.root/name;p.write_bytes(b'INERT_TOOL_NOT_EXECUTED');self.pins.append(pin(p,'tool'))
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj])
        self.logs('link',str(self.obj),[self.obj,self.library],[self.binary])
    def tearDown(self):self.temp.cleanup()
    def logs(self,stem,root,reads,writes,write_root=None):
        for family,content in [('read','^'+root+'\n'+'\n'.join(map(str,reads))),
            ('write','^'+(write_root or root)+'\n'+'\n'.join(map(str,writes))),
            ('command','^'+root+'\n/FIXTURE_POLICY_ONLY')]:
            (self.tracking/(stem+'.'+family+'.1.tlog')).write_text(content+'\n',encoding='utf-16')
    def capture(self):return c.native_parent(self.root,self.output,self.pins,self.tracking,self.binary)
    def test_valid_write_source_root_is_read_provenance(self):
        result=self.capture()
        self.assertEqual(result['selected_outputs'][0]['path'],str(self.binary))
        compile_producer=next(p for p in result['producers'] if p['kind']=='cl')
        self.assertIn(str(self.source),[r['path'] for r in compile_producer['inputs']])
        self.assertEqual([r['path'] for r in compile_producer['outputs']],[str(self.obj)])
    def test_actual_unowned_write_value_still_refused(self):
        import re
        outside=self.root/'outside-output.obj';outside.write_bytes(b'sentinel')
        self.logs('CL',str(self.source),[self.source,self.header],[outside])
        with self.assertRaisesRegex(ValueError,'Unowned native write: '+re.escape(str(outside))):self.capture()
        self.assertEqual(outside.read_bytes(),b'sentinel')
    def test_record_root_cannot_hide_unfrozen_source(self):
        import re
        self.logs('CL',str(self.source),[self.header],[self.obj])
        self.pins=[p for p in self.pins if p['path']!=str(self.source)]
        with self.assertRaisesRegex(ValueError,'Consumed input absent from freeze: '+re.escape(str(self.source))):self.capture()
    def test_unfrozen_read_value_still_refused(self):
        import re
        self.pins=[p for p in self.pins if p['path']!=str(self.header)]
        with self.assertRaisesRegex(ValueError,'Consumed input absent from freeze: '+re.escape(str(self.header))):self.capture()
    def test_write_record_root_must_match_read_record(self):
        self.logs('CL',str(self.source),[self.source,self.header],[self.obj],str(self.header))
        with self.assertRaisesRegex(ValueError,'Write tracking root absent from read records'):self.capture()
    def test_missing_record_header_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text(str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Tracking payload precedes record root'):self.capture()
    def test_empty_record_key_component_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text('^'+str(self.source)+'|\n'+str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Malformed tracking record root'):self.capture()
    def test_relative_record_root_refused(self):
        (self.tracking/'CL.write.1.tlog').write_text('^relative.cpp\n'+str(self.obj)+'\n',encoding='utf-16')
        with self.assertRaisesRegex(ValueError,'Unmodeled tracking record root'):self.capture()
    def test_compound_link_root_preserves_generated_and_frozen_edges(self):
        self.logs('link',str(self.obj)+'|'+str(self.library),[self.obj,self.library],[self.binary])
        self.assertEqual(self.capture()['selected_outputs'][0]['path'],str(self.binary))
    def test_selected_output_missing_refused(self):
        import re
        other=self.output/'other.dll';other.write_bytes(b'fixture')
        self.logs('link',str(self.obj),[self.obj,self.library],[other])
        with self.assertRaisesRegex(ValueError,'Forwarder selected output not actually produced: '+re.escape(str(self.binary))):self.capture()
    def test_generated_read_without_producer_refused(self):
        orphan=self.output/'orphan.obj';orphan.write_bytes(b'unproven')
        self.logs('link',str(orphan),[orphan,self.library],[self.binary])
        with self.assertRaisesRegex(ValueError,'Consumed input absent or unproven generated native edge'):self.capture()
    def test_missing_actual_tool_pin_refused(self):
        self.pins=[p for p in self.pins if Path(p['path']).name!='c2.dll']
        with self.assertRaisesRegex(ValueError,'Actual native tool missing/ambiguous: c2.dll'):self.capture()
    def test_missing_command_tracking_refused(self):
        (self.tracking/'CL.command.1.tlog').unlink()
        with self.assertRaisesRegex(ValueError,'Missing native command/write tracking'):self.capture()
    def test_no_existing_write_value_refused(self):
        self.logs('CL',str(self.source),[self.source,self.header],[self.output/'absent.obj'])
        with self.assertRaisesRegex(ValueError,'Native output evidence missing'):self.capture()
    def test_unknown_tracking_family_refused(self):
        self.logs('unknown',str(self.source),[self.source],[self.obj])
        with self.assertRaisesRegex(ValueError,'Unmodeled native producer: unknown'):self.capture()


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
