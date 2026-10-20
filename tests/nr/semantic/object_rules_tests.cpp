#include "nr/semantic/object_rules/ObjectRules.h"
#include <iostream>
#include <fstream>
#include <cassert>
using namespace Neurotic::Semantic::Rules;
int main() {
    Store store; auto p=EmptyProfile(); auto person=NewRule("person","Human");
    p["rules"].push_back(person); auto r=store.Read();
    assert(store.Commit(r->revision,p,100).accepted);
    assert(!store.Commit(r->revision,p,101).accepted);
    auto a=store.Read(); assert(a->rules.size()==1 && a->rules[0].classes[0]==0);
    assert(!Eligible(a->rules[0],0,.9,99)); assert(Eligible(a->rules[0],0,.9,101));
    p["rules"][0]["label"]="Friendly %s";
    assert(store.Commit(a->revision,p,110).accepted);
    auto b=store.Read();assert(b->rules[0].since==100 && b->semantic==a->semantic);
    p["rules"][0]["match"]["phrases"][0]["text"]="car";
    assert(store.Commit(b->revision,p,120).accepted);
    assert(!Eligible(store.Read()->rules[0],2,.9,110));
    assert(store.Undo(store.Read()->revision,130).accepted);
    assert(store.Read()->rules[0].since==130);
    assert(!Eligible(store.Read()->rules[0],0,.9,125));
    assert(Parse(Export(store.Read()->profile))==store.Read()->profile);
    auto fail=[](std::string text){try{Parse(text);return false;}catch(...){return true;}};
    assert(fail("{\"format\":1,\"format\":2}")); assert(fail(std::string(2*1024*1024+1,' ')));
    assert(fail(std::string(18,'[')+std::string(18,']')));
    auto bad=p;bad["extra"]=1;assert(fail(bad.dump()));
    bad=p;bad["rules"].push_back(bad["rules"][0]);assert(fail(bad.dump()));
    bad=p;bad["rules"][0]["overlay"]["opacity"]=3;assert(fail(bad.dump()));
    bad=p;bad["rules"][0]["requested_controls"]={{{"id","overlay.opacity"},{"value",.5}}};assert(fail(bad.dump()));
    bad=p;bad["rules"][0]["requested_controls"]={{{"id","future.control"},{"value",1}}};assert(Parse(bad.dump())==bad);
    assert(DecodeIni(EncodeIni(p))==p);
    assert(Normalize(" Human ")=="human");assert(Normalize("\xc2\xa0\xe3\x80\x80").empty());assert(Normalize("\xc3\xa9")==Normalize("e\xcc\x81"));
    bad=p;bad["rules"][0]["label"]="\n";assert(fail(bad.dump()));
    assert(fail("{\"rules\":[NaN]}"));assert(fail("{\"rules\":[Infinity]}"));
    std::string invalid=p.dump();invalid.insert(2,1,'\xff');assert(fail(invalid));
    bad=p;bad["rules"][0]["match"]["phrases"][0]["language"]="fr";Store french;assert(french.Commit(0,bad,1).accepted);assert(french.Read()->rules[0].classes.empty());
    auto capacity=EmptyProfile();for(unsigned i=0;i<9;++i)capacity["rules"].push_back(NewRule(std::string(Neurotic::Semantic::Character::ObjectClassNames[i]),"Class "+std::to_string(i)));
    Store bounded;assert(bounded.Commit(0,capacity,1).accepted);assert(bounded.Read()->rules[7].classes.size()==1&&bounded.Read()->rules[8].classes.empty());
    capacity["rules"].push_back(NewRule("person","Another person"));assert(bounded.Commit(bounded.Read()->revision,capacity,2).accepted);assert(bounded.Read()->rules.back().classes.size()==1);
    capacity["rules"][1]["enabled"]=false;assert(bounded.Commit(bounded.Read()->revision,capacity,5).accepted);assert(bounded.Read()->rules[8].since==5&&!bounded.Read()->rules[8].classes.empty());
    auto imported=EmptyProfile(); imported["rules"].push_back(NewRule("grapes","Grapes"));
    auto preview=BuildImport(store.Read(),Export(imported),ImportMode::Copies);
    assert(preview.profile["rules"].size()==2 && !preview.warnings.empty());
    assert(store.Commit(preview.revision,preview.profile,150).accepted);
    assert(store.Read()->rules[1].classes.empty());
    auto sorted=Sorted(*store.Read(),"grap",false,0);assert(sorted.size()==1);
    auto old=store.Read();auto clear=old->profile;clear["rules"]=Json::array();
    assert(store.Commit(old->revision,clear,160).accepted && store.Read()->rules.empty());
    assert(!store.Commit(preview.revision,preview.profile,161).accepted);
    assert(store.Undo(store.Read()->revision,170).accepted);
    for(auto& x:store.Read()->rules)assert(x.since==170);
    std::ifstream example("references/object-rules/20261004/examples/grapes-and-armor.object-rules.json");
    std::string bytes((std::istreambuf_iterator<char>(example)),{}); assert(Parse(bytes)["rules"].size()==2);
    auto large=EmptyProfile();for(int i=0;i<512;++i){auto rule=NewRule("person","Human");for(int c=0;c<11;++c)rule["requested_controls"].push_back({{"id","future."+std::to_string(c)},{"value",std::string(256,'x')}});large["rules"].push_back(rule);}
    assert(large.dump(2).size()>MaxBytes&&large.dump().size()<=MaxBytes);assert(Parse(Export(large))==large);
    std::cout<<"Object Rules core/JSON/revision/late-result/undo/import tests passed\n";
}
