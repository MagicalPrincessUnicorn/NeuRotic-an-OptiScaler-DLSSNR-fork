"""Generate the frozen C++ table from the source-bound normative registry."""
import json, pathlib, sys
packet = pathlib.Path(sys.argv[1])
registry=json.loads((packet/'evidence/CAPABILITY_REGISTRY.json').read_text())
schema=json.loads((packet/'09_CAPABILITY_SCHEMA.json').read_text())
root=pathlib.Path(__file__).resolve().parents[2]
def camel(s): return ''.join(x[:1].upper()+x[1:] for x in s.replace('.','_').split('_'))
rows=registry['ids']
out=['#pragma once','#include "CapabilityContract.h"','namespace DlssNr::Capability {',
     'enum class CapabilityId : uint8_t { '+', '.join(camel(r['id'][7:]) for r in rows)+', Count };',
     'struct Profile { SourceKind source; Basis basis; };',
     'struct RegistryEntry { std::string_view id; AssertionKind kind; uint32_t producers,domains,qualifiers; std::string_view fields; ScalarType scalar; bool scalarRestricted, requiresNoMutation; std::array<Profile,4> profiles; uint8_t profileCount; };',
     'inline constexpr std::array<RegistryEntry,38> Registry{{']
for r in rows:
    def mask(v,t): return '|'.join(f'(1u<<static_cast<unsigned>({t}::{camel(x)}))' for x in v) or '0'
    profiles=','.join('{SourceKind::'+camel(p['source_kind'])+',Basis::'+p['basis'].split('_')[0]+'}' for p in r['direct_evidence_profiles'])
    out.append('{"'+r['id']+'",AssertionKind::'+camel(r['assertion_kind'])+','+mask(r['producers'],'ProducerId')+','+mask(r['required_domains'],'Dimension')+','+mask(r.get('required_qualifiers',[]),'Qualifier')+',"'+'|'.join(r.get('fields',[]))+'",ScalarType::'+camel(r.get('scalar_type','bool'))+','+str('scalar_type' in r).lower()+','+str(r['mutation_policy']=='none_observed_required').lower()+',{{'+profiles+'}},'+str(len(r['direct_evidence_profiles']))+'},')
out+=['}};','inline const RegistryEntry* Lookup(CapabilityId id) noexcept { auto i=static_cast<size_t>(id); return i<Registry.size()?&Registry[i]:nullptr; }',
       'inline uint32_t RequiredDomains(const RegistryEntry& r,std::string_view field) noexcept {',
       ' uint32_t m=r.domains; if(r.id=="nr.cap.fingerprint.field") {']
for prefix,ds in rows[0]['field_prefix_domains'].items(): out+=[' if(field.starts_with("'+prefix+'")) m|='+mask(ds,'Dimension')+';']
out+=[' } return m; }']
for name,values in [('Dimension',schema['$defs']['token']['properties']['dimension']['enum']),('Qualifier',list(schema['$defs']['qualifiers']['properties'])),('SourceKind',schema['$defs']['evidence']['properties']['source_kind']['enum']),('ProducerId',['identity','ngx','config','parameters','native','present','lifecycle','semantic','discovery','auxiliary','provider','hdr','orchestration','qualification','ui'])]:
    out+=['inline constexpr std::array<std::string_view,'+str(len(values))+'> '+name+'Names{'+','.join(json.dumps(x) for x in values)+'};']
out+=['}']
(root/'OptiScaler/nr/diagnostics/capability/CapabilityRegistry.h').write_text('\n'.join(out)+'\n')
(root/'tests/capability/registry.json').write_text(json.dumps(registry,indent=2)+'\n')
(root/'tests/capability/schema-v2.json').write_text(json.dumps(schema,indent=2)+'\n')
wire=json.dumps(schema,separators=(',',':'))
chunks=[wire[i:i+4000] for i in range(0,len(wire),4000)]
(root/'OptiScaler/nr/diagnostics/capability/CapabilityWireSchema.h').write_text('#pragma once\n#include <string_view>\nnamespace DlssNr::Capability {\ninline constexpr std::string_view WireSchemaFragments[]{\n'+',\n'.join('R"schema('+s+')schema"' for s in chunks)+'\n};\n}\n')
(root/'tests/capability/schema-v1.json').write_bytes((packet/'legacy/I01_Capability_Fingerprint_Schema.json').read_bytes())
