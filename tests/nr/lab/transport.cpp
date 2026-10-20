#include "../../../tools/nr/lab/LabCommon.h"
#include <iostream>
int main() {
    using namespace lab;
    int checks=0;
    auto reject=[&](auto fn) { bool rejected=false; try { fn(); } catch(const std::exception&) {rejected=true;} if(!rejected) throw std::runtime_error("MUTANT_ACCEPTED"); ++checks; };
    if(StrictJson("{\"value\":0}")["value"]!=0) return 1; ++checks;
    reject([]{StrictJson("{\"a\":1,\"a\":2}");});
    reject([]{StrictJson("{\"a\":{\"x\":1,\"x\":2}}");});
    reject([]{StrictJson("{\"counter\":9007199254740992}");});
    json schema={{"type","object"},{"additionalProperties",false},{"required",{"count"}},{"properties",{{"count",{{"type","integer"},{"minimum",0}}}}}};
    CheckSchema(json{{"count",0}},schema); ++checks;
    reject([&]{CheckSchema(json{{"count",false}},schema);});
    reject([&]{CheckSchema(json{{"count",-1}},schema);});
    reject([&]{CheckSchema(json{{"count",0},{"extra",true}},schema);});
    reject([]{SafeRelative("../private.dll");});
    reject([]{SafeRelative("C:/private.dll");});
    reject([]{SafeRelative("file.json:stream");});
    if(Sha256("abc")!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 2; ++checks;
    std::cout << "TRANSPORT_PASS checks=" << checks << "\n";
}
