#include "CapabilityWireValidation.h"
#include "CapabilityWireSchema.h"
#include "CapabilityRegistry.h"
#include <charconv>
#include <cmath>
#include <chrono>
#include <regex>
#include <set>
#include <map>
namespace DlssNr::Capability::Wire {
namespace {
const Json& Schema() { static const Json schema=[] {std::string text; for(auto part:WireSchemaFragments) text+=part; return Json::parse(text);}(); return schema; }
bool U64(const Json& value,uint64_t& out) {
    if(!value.is_string()) return false; const auto& s=value.get_ref<const std::string&>(); if(s.empty()||(s.size()>1&&s[0]=='0')) return false;
    auto result=std::from_chars(s.data(),s.data()+s.size(),out); return result.ec==std::errc{}&&result.ptr==s.data()+s.size();
}
bool NumericString(const Json& v,bool signedValue=false) {
    if(!signedValue) { uint64_t n; return U64(v,n); }
    if(!v.is_string()) return false; const auto& s=v.get_ref<const std::string&>(); int64_t n;
    auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);
    return parsed.ec==std::errc{}&&parsed.ptr==s.data()+s.size()&&std::to_string(n)==s;
}
bool Walk(const Json& value,unsigned depth=0,bool legacy=false) {
    if(depth>32) return false;
    if(value.is_string()) return legacy||value.get_ref<const std::string&>().size()<=512;
    if(value.is_number_float()) return std::isfinite(value.get<double>());
    if(value.is_object()) { for(auto it=value.begin();it!=value.end();++it) if(it.key().size()>512||!Walk(it.value(),depth+1,legacy||it.key()=="payload")) return false; }
    else if(value.is_array()) for(const auto& item:value) if(!Walk(item,depth+1,legacy)) return false;
    return true;
}
bool Type(const Json& value,std::string_view type) {
    if(type=="object") return value.is_object(); if(type=="array") return value.is_array();
    if(type=="string") return value.is_string(); if(type=="boolean") return value.is_boolean();
    if(type=="null") return value.is_null(); if(type=="integer") return value.is_number_integer();
    if(type=="number") return value.is_number(); return false;
}
struct ShapeValidator {
    std::map<std::string,std::regex> patterns;
    bool Match(const std::string& value,const std::string& expression) { auto found=patterns.find(expression); if(found==patterns.end()) found=patterns.emplace(expression,std::regex(expression)).first; return std::regex_match(value,found->second); }
    bool Check(const Json& value,const Json& schema,unsigned depth=0) {
        if(depth>128) return false;
        if(schema.is_boolean()) return schema.get<bool>();
        if(schema.contains("$ref")) { auto ref=schema["$ref"].get<std::string>(); if(!ref.starts_with("#/")) return false; return Check(value,Schema().at(Json::json_pointer(ref.substr(1))),depth+1); }
        if(schema.contains("type")) { bool matches=false; auto type=schema["type"]; if(type.is_array()) for(const auto& t:type) matches|=Type(value,t.get<std::string>()); else matches=Type(value,type.get<std::string>()); if(!matches) return false; }
        if(schema.contains("const")&&value!=schema["const"]) return false;
        if(schema.contains("enum")) { bool found=false; for(const auto& v:schema["enum"]) found|=value==v; if(!found) return false; }
        for(const char* operation:{"oneOf","anyOf","allOf"}) if(schema.contains(operation)) { size_t accepted=0; for(const auto& child:schema[operation]) accepted+=Check(value,child,depth+1); if(std::string_view(operation)=="oneOf"&&accepted!=1) return false; if(std::string_view(operation)=="anyOf"&&!accepted) return false; if(std::string_view(operation)=="allOf"&&accepted!=schema[operation].size()) return false; }
        if(schema.contains("not")&&Check(value,schema["not"],depth+1)) return false;
        if(schema.contains("if")) { const char* branch=Check(value,schema["if"],depth+1)?"then":"else"; if(schema.contains(branch)&&!Check(value,schema[branch],depth+1)) return false; }
        if(value.is_object()) {
            if(schema.contains("required")) for(const auto& field:schema["required"]) if(!value.contains(field.get<std::string>())) return false;
            auto properties=schema.value("properties",Json::object());
            for(auto it=value.begin();it!=value.end();++it) {
                if(properties.contains(it.key())) { if(!Check(it.value(),properties[it.key()],depth+1)) return false; }
                else if(schema.contains("additionalProperties")&&!Check(it.value(),schema["additionalProperties"],depth+1)) return false;
            }
        }
        if(value.is_array()) {
            if(schema.contains("minItems")&&value.size()<schema["minItems"].get<size_t>()) return false;
            if(schema.contains("maxItems")&&value.size()>schema["maxItems"].get<size_t>()) return false;
            if(schema.contains("items")) for(const auto& item:value) if(!Check(item,schema["items"],depth+1)) return false;
            if(schema.value("uniqueItems",false)) for(size_t i=0;i<value.size();++i) for(size_t j=0;j<i;++j) if(value[i]==value[j]) return false;
        }
        if(value.is_string()) {
            const auto& text=value.get_ref<const std::string&>();
            const auto characters=std::count_if(text.begin(),text.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
            if(schema.contains("minLength")&&static_cast<size_t>(characters)<schema["minLength"].get<size_t>()) return false;
            if(schema.contains("maxLength")&&static_cast<size_t>(characters)>schema["maxLength"].get<size_t>()) return false;
            if(schema.contains("pattern")&&!Match(text,schema["pattern"].get<std::string>())) return false;
            if(schema.contains("format")) {
                auto format=schema["format"].get<std::string>();
                if(format=="uuid"&&!Match(text,"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")) return false;
                if(format=="date-time") {
                    if(!Match(text,"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})")) return false;
                    auto integer=[&](size_t p,size_t n){return std::stoi(text.substr(p,n));};
                    std::chrono::year_month_day day{std::chrono::year(integer(0,4)),std::chrono::month(static_cast<unsigned>(integer(5,2))),std::chrono::day(static_cast<unsigned>(integer(8,2)))};
                    if(!day.ok()||integer(11,2)>23||integer(14,2)>59||integer(17,2)>60) return false;
                }
            }
        }
        if(value.is_number()) {
            if(schema.contains("minimum")&&value.get<double>()<schema["minimum"].get<double>()) return false;
            if(schema.contains("maximum")&&value.get<double>()>schema["maximum"].get<double>()) return false;
        }
        return true;
    }
};
using Records=std::map<std::string,const Json*>;
bool Index(const Json& array,std::string_view key,Records& index) { for(const auto& row:array) if(!index.emplace(row.at(std::string(key)).get<std::string>(),&row).second) return false; return true; }
bool Covered(const Json& claim,const Json& source) {
    if(claim["origin_session"]!=source["origin_session"]) return false;
    for(const auto& stamp:source["bindings"]) { bool found=false; for(const auto& token:claim["bindings"]) if(token["dimension"]==stamp["dimension"]) { found=token["status"]=="unknown"||token==stamp; break; } if(!found) return false; } return true;
}
bool Graph(const Records& records,std::string_view parentField) {
    std::map<std::string,unsigned> depths; std::set<std::string> visiting;
    auto visit=[&](auto&& self,const std::string& id)->unsigned {
        if(depths.contains(id)) return depths[id]; if(visiting.contains(id)||visiting.size()>=16) return 17; visiting.insert(id);
        unsigned depth=1; for(const auto& parent:records.at(id)->at(std::string(parentField))) { auto key=parent.get<std::string>(); if(records.contains(key)) depth=std::max(depth,1+self(self,key)); if(depth>16) break; }
        visiting.erase(id); depths[id]=depth; return depth;
    };
    for(const auto& row:records) if(visit(visit,row.first)>16) return false; return true;
}
}
Json Parse(std::string_view text) {
    if(text.size()>8*1024*1024) throw std::length_error("CAP_DOCUMENT_CAPACITY");
    std::vector<std::set<std::string>> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json& value) {
        if(depth>32) throw std::length_error("CAP_DEPTH_CAPACITY");
        if(event==Json::parse_event_t::object_start) keys.emplace_back();
        if(event==Json::parse_event_t::key) { if(keys.empty()||!keys.back().insert(value.get<std::string>()).second) throw std::invalid_argument("CAP_DUPLICATE_JSON_KEY"); }
        if(event==Json::parse_event_t::object_end) keys.pop_back(); return true;
    };
    return Json::parse(text,callback,true,false);
}
bool Shape(const Json& doc) { ShapeValidator validator; return Walk(doc)&&validator.Check(doc,Schema()); }
bool LegacyShape(const Json& doc) { ShapeValidator validator; return Walk(doc,0,true)&&validator.Check(doc,Schema()["$defs"]["legacyFingerprint"]); }
bool Semantics(const Json& doc) {
    uint64_t revision; if(!U64(doc["snapshot_revision"],revision)) return false;
    Records scopes,evidence,facts,streams;
    if(!Index(doc["scopes"],"scope_id",scopes)||!Index(doc["evidence"],"evidence_id",evidence)||!Index(doc["facts"],"fact_id",facts)||!Index(doc["streams"],"writer_id",streams)) return false;
    for(const auto& row:scopes) {
        const auto& scope=*row.second; std::set<std::string> dimensions;
        for(const auto& token:scope["bindings"]) { if(!dimensions.insert(token["dimension"]).second) return false; uint64_t n; if(token["status"]=="known"&&!U64(token["generation"],n)) return false; }
    }
    for(const auto& row:streams) {
        const auto& stream=*row.second; uint64_t offered,committed;
        if(!U64(stream["offered_sequence"],offered)||!U64(stream["committed_sequence"],committed)||committed>offered||(stream["state"]=="complete"&&offered!=committed)||(stream["state"]=="gap"&&offered==committed)) return false;
        std::set<std::string> scopeRefs; for(const auto& s:stream["scope_refs"]) if(!scopes.contains(s.get<std::string>())||!scopeRefs.insert(s.get<std::string>()).second) return false;
    }
    auto owned=[&](const Json& record) {
        auto w=record["writer_id"].get<std::string>(); auto s=record["scope_ref"].get<std::string>(); uint64_t seq;
        if(!streams.contains(w)||!scopes.contains(s)||!U64(record["source_sequence"],seq)||!seq||record["source_sequence"]!=streams[w]->at("committed_sequence")) return false;
        if(scopes[s]->at("origin_session")!=streams[w]->at("origin_session")||streams[w]->at("origin_session")!=doc["origin_session"]) return false;
        auto refs=streams[w]->at("scope_refs"); return std::find(refs.begin(),refs.end(),s)!=refs.end();
    };
    for(const auto& row:evidence) {
        const auto& e=*row.second; if(!owned(e)) return false;
        for(const auto& parent:e["parent_evidence_ids"]) { auto p=parent.get<std::string>(); if(!evidence.contains(p)||!Covered(*scopes[e["scope_ref"]],*scopes[evidence[p]->at("scope_ref")])) return false; }
    }
    for(const auto& row:facts) {
        const auto& f=*row.second; if(!owned(f)) return false;
        auto id=f["capability_id"].get<std::string>(); const RegistryEntry* registry=nullptr; for(const auto& r:Registry) if(r.id==id) registry=&r; if(!registry) return false;
        const auto& stream=*streams[f["writer_id"]]; unsigned producer=0; for(;producer<ProducerIdNames.size();++producer) if(ProducerIdNames[producer]==stream["producer_id"].get<std::string>()) break;
        if(producer>=ProducerIdNames.size()||!(registry->producers&(1u<<producer))) return false;
        const auto& qualifiers=f["qualifiers"]; for(unsigned q=0;q<14;++q) if((registry->qualifiers&(1u<<q))&&!qualifiers.contains(std::string(QualifierNames[q]))) return false;
        auto field=qualifiers.value("field",std::string{}); auto required=RequiredDomains(*registry,field); uint32_t present=0;
        if(id=="nr.cap.fingerprint.field") { bool prefix=false; for(auto p:{"/collector/","/subject/","/adapter/","/driver/","/modules/","/feature_target/"}) prefix|=field.starts_with(p); if(!prefix) return false; }
        for(const auto& token:scopes[f["scope_ref"]]->at("bindings")) for(unsigned d=0;d<22;++d) if(DimensionNames[d]==token["dimension"].get<std::string>()) { present|=1u<<d; if((required&(1u<<d))&&token["status"]=="not_applicable") return false; }
        if((required&present)!=required) return false;
        if(!registry->fields.empty()) { auto fields=registry->fields; bool found=false; while(!fields.empty()) { auto split=fields.find('|'); found|=fields.substr(0,split)==field; if(split==std::string_view::npos) break; fields.remove_prefix(split+1); } if(!found) return false; }
        if(id=="nr.cap.present.counter"&&qualifiers.value("scope_category","")!="process_summary") return false;
        if(qualifiers.contains("feature_id")) { uint64_t feature; if(!U64(qualifiers["feature_id"],feature)||feature>UINT32_MAX) return false; }
        if(qualifiers.contains("parameter_phase")) { auto phase=qualifiers["parameter_phase"].get<std::string>(); if(phase!="declaration"&&phase!="creation"&&phase!="evaluation"&&phase!="readback") return false; }
        const auto& assertion=f["assertion"];
        if(!assertion.is_null()) {
            constexpr std::array<std::string_view,8> kinds{"support","availability","enablement","health","result","qualification","owner_receipt","scalar"};
            if(kinds[static_cast<size_t>(registry->kind)]!=assertion["kind"].get<std::string>()) return false;
            if(assertion["kind"]=="scalar") {
                auto type=assertion["scalar_type"].get<std::string>(); constexpr std::array<std::string_view,5> types{"bool","u64","i64","f64","text"};
                if(registry->scalarRestricted&&type!=types[static_cast<size_t>(registry->scalar)]) return false;
                if((type=="u64"&&!NumericString(assertion["value"]))||(type=="i64"&&!NumericString(assertion["value"],true))) return false;
            }
        }
        for(const auto& ref:f["evidence_ids"]) { auto e=ref.get<std::string>(); if(!evidence.contains(e)||!Covered(*scopes[f["scope_ref"]],*scopes[evidence[e]->at("scope_ref")])) return false; }
        for(const auto& ref:f["dependency_fact_ids"]) { auto dep=ref.get<std::string>(); if(facts.contains(dep)&&!Covered(*scopes[f["scope_ref"]],*scopes[facts[dep]->at("scope_ref")])) return false; }
    }
    for(const auto& row:streams) {
        Json fs=Json::array(),es=Json::array(),ss=Json::array();
        for(const auto& item:facts) if(item.second->at("writer_id")==row.first) fs.push_back(*item.second);
        for(const auto& item:evidence) if(item.second->at("writer_id")==row.first) es.push_back(*item.second);
        for(const auto& s:row.second->at("scope_refs")) ss.push_back(*scopes[s.get<std::string>()]);
        if(fs.size()>32||es.size()>64||ss.size()>32||Json::array({fs,es,ss}).dump().size()>65536) return false;
    }
    return Graph(evidence,"parent_evidence_ids")&&Graph(facts,"dependency_fact_ids");
}
}
