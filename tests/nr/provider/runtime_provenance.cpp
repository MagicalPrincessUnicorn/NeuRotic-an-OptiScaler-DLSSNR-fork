#include <iostream>
#if __has_include(<nr/diagnostics/RuntimeProvenance.h>)
#include <nr/diagnostics/RuntimeProvenance.h>
int main(){
 namespace P=Neurotic::Diagnostics::RuntimeProvenance;
 int checks=0,failed=0;auto check=[&](bool ok,const char* text){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<text<<'\n';}};
 auto absent=P::Module(nullptr);check(absent["observed"]==false&&absent["sha256"].is_null(),"absent module is unknown, not executable fallback");
 auto exe=P::Module(GetModuleHandleW(nullptr));check(exe["observed"]==true&&!exe["sha256"].is_null()&&exe["bytes"].get<std::uint64_t>()>0,"loaded executable has observed path and disk hash");
 auto where=P::Address(reinterpret_cast<const void*>(&main));check(where["module"]["path"]==exe["path"],"function address resolves actual image");
 auto unknown=P::Address(nullptr);check(unknown["module"]["observed"]==false,"null API address remains unknown");
 int calls=0;P::Json event,entry;
 SetLastError(123);
 auto result=P::Call([&](P::Json e){if(e["phase"]=="enter")entry=e;event=std::move(e);SetLastError(999);},"L3","slIsFeatureSupported",[&]{
     check(GetLastError()==123,"entry diagnostics preserve host last-error");++calls;SetLastError(456);return 32;});
 check(GetLastError()==456,"return diagnostics preserve API last-error");
 check(entry["call_id"]==event["call_id"]&&entry["thread_id"]==event["thread_id"],"nested calls can be paired by ID and thread");
 check(result==32&&calls==1&&event["result"]==32&&event["stage"]=="L3","exact error and API stage preserved without retry");
 check(P::Call([](P::Json){throw 1;},"L2","slInit",[&]{++calls;return 0;})==0&&calls==2,"diagnostic sink failure cannot change API result");
 bool threw=false;try{P::Call([](P::Json){},"L4","slSetD3DDevice",[]{throw 7;return 0;});}catch(int){threw=true;}
 check(threw,"API exception is not converted to success");
 std::cout<<"Runtime provenance: "<<checks<<" checks, "<<failed<<" failures; CPU only\n";return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL passive runtime provenance implementation missing\n";return 1;}
#endif
