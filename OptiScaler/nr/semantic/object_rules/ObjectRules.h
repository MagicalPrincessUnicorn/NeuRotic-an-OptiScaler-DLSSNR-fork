#pragma once
#include "ObjectRuleSchema.h"
#include "../character/CharacterClassNames.h"
#include <json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"normaliz.lib")

namespace Neurotic::Semantic::Rules {
using Json=nlohmann::json;
inline constexpr size_t MaxBytes=2*1024*1024;
inline uint64_t Now() {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline std::string Id(){GUID g{};Require(SUCCEEDED(CoCreateGuid(&g)),"Cannot create a rule ID");wchar_t out[40]{};StringFromGUID2(g,out,40);std::string s;for(int i=1;i<37;++i)s+=static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));return s;}
inline std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);Require(n>0||s.empty(),"Invalid UTF-8 text");std::wstring w(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
inline std::string Utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);Require(n>0||s.empty(),"Invalid Unicode text");std::string v(n,' ');if(n)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),v.data(),n,nullptr,nullptr);return v;}
inline std::string Normalize(const std::string& s){auto w=Wide(s);if(w.empty())return {};int n=NormalizeString(NormalizationC,w.data(),static_cast<int>(w.size()),nullptr,0);Require(n>0,"Cannot normalize text");std::wstring norm(n,L' ');n=NormalizeString(NormalizationC,w.data(),static_cast<int>(w.size()),norm.data(),n);Require(n>0,"Cannot normalize text");norm.resize(n);std::wstring lower(norm.size(),L' ');Require(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,norm.data(),n,lower.data(),n,nullptr,nullptr,0)>0,"Cannot normalize text");const auto whitespace=L" \t\r\n\v\f\u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";auto a=lower.find_first_not_of(whitespace),b=lower.find_last_not_of(whitespace);return a==std::wstring::npos?std::string():Utf8(lower.substr(a,b-a+1));}
inline bool SchemaMatches(const Json& v,const Json& schema){
    if(schema.contains("oneOf")){size_t n=0;for(const auto& s:schema["oneOf"])if(SchemaMatches(v,s))++n;return n==1;}
    if(schema.contains("const")&&v!=schema["const"])return false;
    if(schema.contains("enum")&&std::find(schema["enum"].begin(),schema["enum"].end(),v)==schema["enum"].end())return false;
    auto type=schema.value("type","");
    if(type=="object"){
        if(!v.is_object())return false;
        for(const auto& k:schema.value("required",Json::array()))if(!v.contains(k.get<std::string>()))return false;
        const auto props=schema.value("properties",Json::object());
        for(const auto& x:v.items()){if(!props.contains(x.key()))return false;if(!SchemaMatches(x.value(),props[x.key()]))return false;}
    }else if(type=="array"){
        if(!v.is_array()||v.size()<schema.value("minItems",size_t(0))||v.size()>schema.value("maxItems",size_t(512)))return false;
        for(const auto& x:v)if(!SchemaMatches(x,schema["items"]))return false;
    }else if(type=="string"){
        if(!v.is_string())return false;const auto s=v.get<std::string>();const auto w=Wide(s);size_t length=0;
        for(auto c:w){if(c<32||(c>=127&&c<=159))return false;if(c<0xDC00||c>0xDFFF)++length;}
        if(length<schema.value("minLength",size_t(0))||length>schema.value("maxLength",size_t(4096)))return false;
        if(schema.contains("pattern")&&!std::regex_match(s,std::regex(schema["pattern"].get<std::string>())))return false;
    }else if(type=="boolean"){if(!v.is_boolean())return false;}
    else if(type=="number"||type=="integer"){
        if(!v.is_number()||(type=="integer"&&!v.is_number_integer()))return false;
        double n=v.get<double>();if(!std::isfinite(n)||n<schema.value("minimum",-1e100)||n>schema.value("maximum",1e100))return false;
    }
    return true;
}
inline void Validate(const Json& p){
    static const auto schema=Json::parse(SchemaText);
    Require(SchemaMatches(p,schema),"Profile does not match Object Rules v1 (field, type or range)");
    Require(!Normalize(p["profile"]["name"]).empty(),"Profile name is blank");std::set<std::string> ids;
    for(const auto& r:p["rules"]){
        Require(ids.insert(r["id"]).second,"Duplicate rule ID");Require(!Normalize(r["label"]).empty(),"Rule label is blank");
        if(r["match"]["kind"]=="text")for(const auto& q:r["match"]["phrases"]){Require(!Normalize(q["text"]).empty(),"Phrase is blank");Require(std::regex_match(q["language"].get<std::string>(),std::regex("[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*")),"Invalid language tag");}
        std::set<std::string> controls;
        for(const auto& c:r["requested_controls"]){const auto id=c["id"].get<std::string>();Require(controls.insert(id).second,"Duplicate requested control");Require(id.rfind("overlay.",0)!=0&&id.rfind("nr.",0)!=0&&id.rfind("appearance.",0)!=0,"Built-in control must use its dedicated field");}
    }
    Require(p.dump().size()<=MaxBytes,"Profile exceeds 2 MiB");
}
inline Json Parse(const std::string& bytes){
    Require(bytes.size()<=MaxBytes,"Profile exceeds 2 MiB");std::vector<std::set<std::string>> keys;
    auto p=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
        Require(depth<=16,"Profile nesting exceeds 16");
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        if(event==Json::parse_event_t::key)Require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"Duplicate JSON key");
        if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
    });Validate(p);return p;
}
inline std::string Export(const Json& p){Validate(p);auto text=p.dump(2);return text.size()<=MaxBytes?text:p.dump();}
inline std::string EncodeIni(const Json& p){Validate(p);const auto text=p.dump();std::string out;out.reserve(text.size()*2);for(unsigned char c:text){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
inline Json DecodeIni(const std::string& text){Require(text.size()<=MaxBytes*2&&text.size()%2==0,"Invalid saved Object Rules size");auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Invalid saved Object Rules encoding");};std::string bytes;bytes.reserve(text.size()/2);for(size_t i=0;i<text.size();i+=2)bytes+=static_cast<char>((digit(text[i])<<4)|digit(text[i+1]));return Parse(bytes);}
inline Json EmptyProfile(){return {{"format","neurotic.object-rules"},{"schema_version",1},{"profile",{{"id",Id()},{"name","Object Rules"}}},{"rules",Json::array()}};}
inline Json NewRule(const std::string& phrase,const std::string& label){return {{"id",Id()},{"label",label},{"enabled",true},{"match",{{"kind","text"},{"phrases",Json::array({{{"text",phrase},{"language","en"}}})},{"threshold",.5},{"max_instances",16}}},{"scope","all_visible_matches"},{"priority",100},{"overlay",{{"show_box",true},{"show_label",true},{"box_color_srgb","#3adcff"},{"label_color_srgb","#ffffff"},{"line_width_ui",2},{"opacity",1}}},{"nr",{{"strategy","inherit"},{"blend_amount",1}}},{"appearance",{{"enabled",false},{"tint_color_srgb","#ffffff"},{"tint_amount",0},{"saturation",1},{"exposure_ev",0}}},{"requested_controls",Json::array()}};}
inline Json DefaultProfile(){auto p=EmptyProfile();for(const char* word:{"Human","Face","Armor","Robot","Bush","Duck","Goose","Grapes"}){auto r=NewRule(word,word);r["enabled"]=false;p["rules"].push_back(r);}return p;}
struct Rule {std::string id,label,status;std::vector<unsigned> classes;Json value;uint64_t since=0,semantic=0;bool enabled=false,showBox=true,showLabel=true;unsigned boxRgb=0,labelRgb=0,maxInstances=0,priority=0;float lineWidth=2,opacity=1;double threshold=1;};
inline std::vector<unsigned> Classes(const Json& match){
    std::vector<std::string> words;auto kind=match["kind"].get<std::string>();
    if(kind=="text")for(const auto& q:match["phrases"])if(q["language"]=="en")words.push_back(Normalize(q["text"]));
    if(kind=="fixed_class"&&match["provider_key"]=="opencv-zoo-nanodet-coco80")words.push_back(Normalize(match["class_key"]));
    std::vector<unsigned> out;for(auto word:words){if(word=="human")word="person";for(unsigned i=0;i<Character::ObjectClassNames.size();++i)if(word==Character::ObjectClassNames[i]&&std::find(out.begin(),out.end(),i)==out.end())out.push_back(i);}return out;
}
struct Snapshot {Json profile;uint64_t revision=0,semantic=0,overlay=0,effect=0;std::vector<Rule> rules;};
struct Ack {bool accepted=false;std::string reason;};
class Store {
    std::atomic<std::shared_ptr<const Snapshot>> current_;std::mutex edit_;std::vector<Json> undo_;
    Ack Apply(uint64_t revision,const Json& p,uint64_t now,bool undo){
        auto old=Read();if(revision!=old->revision)return {false,"Settings changed. Reload before applying this edit."};
        try{Validate(p);}catch(const std::exception& e){return {false,e.what()};}
        auto next=std::make_shared<Snapshot>();next->profile=p;next->revision=old->revision+1;next->semantic=old->semantic;next->overlay=old->overlay;next->effect=old->effect;
        bool semantic=undo,overlay=false,effect=false;std::set<unsigned> active;
        for(const auto& v:p["rules"]){Rule r;r.id=v["id"];r.label=v["label"];r.enabled=v["enabled"];r.value=v;r.classes=Classes(v["match"]);
            const auto& o=v["overlay"];r.showBox=o["show_box"];r.showLabel=o["show_label"];r.boxRgb=std::stoul(o["box_color_srgb"].get<std::string>().substr(1),nullptr,16);r.labelRgb=std::stoul(o["label_color_srgb"].get<std::string>().substr(1),nullptr,16);r.lineWidth=o["line_width_ui"];r.opacity=o["opacity"];r.maxInstances=v["match"].value("max_instances",0u);r.threshold=v["match"].value("threshold",1.0);r.priority=v["priority"];
            auto before=std::find_if(old->rules.begin(),old->rules.end(),[&](const Rule& b){return b.id==r.id;});
            bool changed=undo||before==old->rules.end()||before->value["match"]!=v["match"]||before->enabled!=r.enabled;
            r.since=changed?now:before->since;r.semantic=changed?next->revision:before->semantic;semantic|=changed;
            overlay|=before==old->rules.end()||before->value["overlay"]!=v["overlay"]||before->label!=r.label;
            effect|=before==old->rules.end()||before->value["nr"]!=v["nr"]||before->value["appearance"]!=v["appearance"]||before->value["priority"]!=v["priority"];
            if(!r.enabled)r.status="Off";else if(r.classes.empty())r.status="Not supported by this detector";else{
                auto proposed=active;proposed.insert(r.classes.begin(),r.classes.end());if(proposed.size()>8){r.classes.clear();r.status="Waiting: active class limit";}else{active=std::move(proposed);r.status="Ready to match fixed detector classes";}}
            // A capacity change may activate a formerly waiting rule without a phrase edit.
            if(before!=old->rules.end()&&before->classes!=r.classes){r.since=now;r.semantic=next->revision;semantic=true;}
            next->rules.push_back(std::move(r));
        }
        semantic|=next->rules.size()!=old->rules.size();next->semantic+=semantic;next->overlay+=overlay;next->effect+=effect;
        if(!undo){if(undo_.size()==16)undo_.erase(undo_.begin());undo_.push_back(old->profile);}current_.store(next);return {true,"Updated"};
    }
public:
    Store(){auto s=std::make_shared<Snapshot>();s->profile=EmptyProfile();current_.store(s);}
    std::shared_ptr<const Snapshot> Read()const{return current_.load();}
    Ack Commit(uint64_t revision,const Json& p,uint64_t now=Now()){std::lock_guard lock(edit_);return Apply(revision,p,now,false);}
    Ack Undo(uint64_t revision,uint64_t now=Now()){std::lock_guard lock(edit_);if(undo_.empty())return {false,"Nothing to undo"};auto a=Apply(revision,undo_.back(),now,true);if(a.accepted)undo_.pop_back();return a;}
};
inline bool Eligible(const Rule& rule,unsigned classId,double score,uint64_t captureNs){return rule.enabled&&captureNs>rule.since&&std::find(rule.classes.begin(),rule.classes.end(),classId)!=rule.classes.end()&&score>=rule.threshold;}
inline std::vector<size_t> Sorted(const Snapshot& s,const std::string& search,bool descending,int filter){std::vector<size_t> out;const auto term=Normalize(search);for(size_t i=0;i<s.rules.size();++i){const auto& r=s.rules[i];if(filter==1&&!r.enabled)continue;if(filter==2&&r.enabled)continue;if(filter==3&&(!r.enabled||!r.classes.empty()))continue;auto hay=Normalize(r.label+" "+r.value["match"].dump());if(hay.find(term)!=std::string::npos)out.push_back(i);}std::stable_sort(out.begin(),out.end(),[&](size_t a,size_t b){auto x=Normalize(s.rules[a].label),y=Normalize(s.rules[b].label);return x==y?s.rules[a].id<s.rules[b].id:descending?x>y:x<y;});return out;}
enum class ImportMode {Copies,UpdateIds,Replace};
struct ImportPreview {Json profile;uint64_t revision=0;std::vector<std::string> warnings;};
inline ImportPreview BuildImport(std::shared_ptr<const Snapshot> current,const std::string& bytes,ImportMode mode){
    const auto input=Parse(bytes);ImportPreview out{current->profile,current->revision,{}};
    std::map<std::string,std::string> phrases;
    for(const auto& r:current->profile["rules"])if(r["match"]["kind"]=="text")for(const auto& q:r["match"]["phrases"])phrases[Normalize(q["text"])]=r["id"];
    for(const auto& r:input["rules"]){
        if(r["match"]["kind"]=="text")for(const auto& q:r["match"]["phrases"]){auto found=phrases.find(Normalize(q["text"]));if(found!=phrases.end()&&(found->second!=r["id"].get<std::string>()||mode==ImportMode::Copies))out.warnings.push_back(r["label"].get<std::string>()+": same words already exist; both rule IDs are retained unless replacing");}
        if(Classes(r["match"]).empty())out.warnings.push_back(r["label"].get<std::string>()+": detector cannot match this rule");
        if(!r["requested_controls"].empty()||r["nr"]["strategy"]!="inherit"||r["appearance"]["enabled"]==true)out.warnings.push_back(r["label"].get<std::string>()+": regional effects/extra controls retained inactive");
    }
    if(mode==ImportMode::Replace)out.profile=input;
    else for(auto r:input["rules"]){
        if(mode==ImportMode::Copies)r["id"]=Id();auto& rules=out.profile["rules"];
        auto found=std::find_if(rules.begin(),rules.end(),[&](const Json& x){return x["id"]==r["id"];});
        if(found==rules.end())rules.push_back(r);else{out.warnings.push_back(r["label"].get<std::string>()+": updates existing rule "+r["id"].get<std::string>());*found=r;}
    }
    Validate(out.profile);std::set<unsigned> active;
    for(const auto& r:out.profile["rules"])if(r["enabled"].get<bool>()){auto classes=Classes(r["match"]);active.insert(classes.begin(),classes.end());}
    out.warnings.insert(out.warnings.begin(),std::to_string(active.size())+" unique supported classes requested; at most 8 active, identical classes share detections.");
    return out;
}
// One game process owner; desktop uses per-selected-target stores and offline save transactions.
struct OverlaySelection {bool armed=false;std::string pending;};
inline OverlaySelection& Selection(){static OverlaySelection selection;return selection;}
inline std::string& LoadError(){static std::string s;return s;}
inline Store& GameStore(){static Store s;return s;}
inline std::atomic<int>& Activity(){static std::atomic<int> a{0};return a;}
}
