#include <cstdio>
#include <cstdlib>
#include <dlssnr/NativeParameterOverride.h>
#if __has_include(<dlssnr/NrBridgeParameters.h>)
#include <dlssnr/NrBridgeParameters.h>
#define HAS_BRIDGE_PARAMETERS
#endif
struct Resource { unsigned width; };
// NGX is external. Model the original D3D11 table's separate pointer lanes;
// a D3D12 setter need not be supported by that table.
struct Parameters
{
    void* primary=nullptr; void* alias=nullptr; Resource* typed=nullptr;
    bool ignore=false; unsigned typedReads=0;
    int Get(const char* key,void** out) { *out=key[0]=='O'?primary:alias; return 1; }
    int Get(const char*,Resource** out) { ++typedReads; *out=typed; return 1; }
    void Set(const char*,void* value) { if(!ignore) primary=value; }
    void Set(const char*,Resource*) {} // unsupported D3D12 lane on a D3D11 table
};
static unsigned checks=0;
static void Check(bool ok,const char* reason)
{ ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);} }
int main()
{
#ifndef HAS_BRIDGE_PARAMETERS
    Check(false,"bridge-owned pointer lane unavailable");
#else
    namespace B=DlssNr::Bridge;
    Resource display{3840},scratch{2560},stale{1920};
    Parameters p{&display,&stale,&stale};
    auto read=[&](bool bridge){return B::ReadResource<Resource>(&p,"Output","alias",1,bridge);};
    Check(read(false)==&stale,"native D3D12 still uses its typed lane");
    p.typedReads=0;
    Check(read(true)==&display&&p.typedReads==0,"bridge ignores stale typed/alias lanes");
    {
        DlssNr::NativeParameterOverride binding(&p,"Output",static_cast<void*>(&display),1);
        Check(binding.Bind(&scratch),"void override verified despite unsupported typed setter");
        Check(read(true)==&scratch&&read(true)->width==2560,"model receives render-sized Pre-SR scratch");
        Check(binding.Restore()&&read(true)==&display,"SR receives original full output after restore");
    }
    Check(read(true)==&display,"scope preserves restored output");
    p.primary=nullptr;
    Check(read(true)==nullptr,"missing primary bridge input refuses stale alias");
    p.primary=&display;p.ignore=true;
    {DlssNr::NativeParameterOverride binding(&p,"Output",static_cast<void*>(&display),1);
     Check(!binding.Bind(&scratch)&&read(true)==&display,"ignored write refuses without dispatching into display");}
    p.ignore=false;
    {DlssNr::NativeParameterOverride binding(&p,"Output",static_cast<void*>(&display),1);
     Check(binding.Bind(&scratch),"restore-fault fixture bound");p.ignore=true;
     Check(!binding.Restore(),"failed restoration remains explicit for outer SR admission");}
#endif
    std::printf("PASS: %u bridge parameter-lane checks\n",checks);
}
