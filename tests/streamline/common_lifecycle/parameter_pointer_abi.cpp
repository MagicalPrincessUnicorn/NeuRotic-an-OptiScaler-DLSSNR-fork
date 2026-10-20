// Public interface/native compiler witness, never an opaque legacy object call.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>
#include <stdexcept>
#include <sl.param/parameters.h>
#include "source_set_pointer_type.inc"
using ExpectedSetPointer=void(*)(void*,const char*,void*);
static_assert(std::is_same_v<SourceSetPointer,ExpectedSetPointer>,
    "actual product pointer setter must return void and receive pointer VALUE, not void**");
using PublicMember=void(sl::param::IParameters::*)(const char*,void*);
static_assert(std::is_same_v<decltype(static_cast<PublicMember>(&sl::param::IParameters::set)),PublicMember>);

struct Params final:sl::param::IParameters
{
    void* value=nullptr;const char* key=nullptr;unsigned pointerSets=0;
#define VALUE(T) void set(const char*,T)override{} bool get(const char*,T*)const override{return false;}
    VALUE(bool) VALUE(unsigned long long) VALUE(float) VALUE(double) VALUE(unsigned int) VALUE(int)
#undef VALUE
    void set(const char* name,void* pointer)override {key=name;value=pointer;++pointerSets;}
    bool get(const char* name,void** pointer)const override
    {if(!key||std::strcmp(name,key))return false;*pointer=value;return true;}
    std::vector<std::string> enumerate()const override{return {};}
};
// Retain compiler assembly for these exact actual public virtual calls.
extern "C" __declspec(noinline) void NrPublicPointerSet(sl::param::IParameters* p,const char* key,void* value)
{p->set(key,value);}
extern "C" __declspec(noinline) bool NrPublicPointerGet(const sl::param::IParameters* p,const char* key,void** value)
{return p->get(key,value);}
static void PointerOriginal(void* self,const char* key,void* value)
{NrPublicPointerSet(static_cast<sl::param::IParameters*>(self),key,value);}
static unsigned checks=0;
static void Check(bool ok,const char* text)
{++checks;if(!ok)throw std::runtime_error(text);std::printf("PASS %s\n",text);}
int main()
{
    try
    {
        Params p;std::uint64_t payload=0xdeadbeef11223344ull;void* observed=nullptr;
        NrPublicPointerSet(&p,sl::param::common::kSystemCaps,&payload);
        Check(p.value==&payload&&p.pointerSets==1,"actual public virtual setter stores pointer value exactly once");
        Check(NrPublicPointerGet(&p,sl::param::common::kSystemCaps,&observed)&&observed==&payload,
            "actual public virtual getter receives pointer-to-pointer output");
        const ExpectedSetPointer typed=&PointerOriginal;
        typed(static_cast<sl::param::IParameters*>(&p),"unrelated.key",&payload);
        Check(p.value==&payload&&p.pointerSets==2&&!std::strcmp(p.key,"unrelated.key"),
            "declared free original-call signature forwards self key and value without double indirection");
        typed(static_cast<sl::param::IParameters*>(&p),"nullable.key",nullptr);
        Check(p.value==nullptr&&p.pointerSets==3,"null pointer value forwards unchanged");
        Check(payload==0xdeadbeef11223344ull,"pointer publication never writes through or fabricates payload layout");
        std::printf("PARAMETER_POINTER_ABI checks=%u PASS publicIParameters=1 opaqueLegacy=0 rawVtableCall=0 privateProvider=0 GPU=0\n",checks);
        return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"PARAMETER_POINTER_ABI FAIL: %s\n",e.what());return 1;}
}
