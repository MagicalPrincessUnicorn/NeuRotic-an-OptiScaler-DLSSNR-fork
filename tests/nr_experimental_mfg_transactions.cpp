#include "../external/mfg_ampere/src/addons/mfgunlock/blackwell_temporal.hpp"
#include <cstdio>
#include <stdexcept>
namespace VW = mfgunlock::validatedwarp;
namespace PM = provider_mock;
static void Check(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
struct Fixture {
    alignas(8) uint64_t slots[2]{0x1234,0x1234};
    uint8_t source[16]{};
    VW::internal::PreparedRedirect plan;
    Fixture() {
        PM::regions.push_back({slots,sizeof(slots),MEM_IMAGE,PAGE_READONLY});
        plan.module=slots;plan.located.address=source;plan.located.size=sizeof(source);
        plan.expected=0x1234;plan.slots={&slots[0],&slots[1]};plan.rebuilt.resize(16);
    }
    ~Fixture() { PM::on_protect=nullptr;std::erase_if(PM::regions,[this](auto r){return r.base==slots;}); }
};
int main() {
    try {
        {
            Fixture f;VW::Redirect redirect;std::string detail;
            Check(VW::internal::CommitPreparedRedirect(f.plan,"test",redirect,detail),"commit fixture");
            std::vector<VW::Redirect> redirects;redirects.push_back(std::move(redirect));
            auto allocation=redirects[0].allocation;f.slots[0]=0xdead;
            Check(!VW::Restore(redirects),"foreign descriptor must refuse rollback");
            Check(f.slots[0]==0xdead && PM::allocations.contains(allocation),"foreign descriptor preserved and backing retained");
            f.slots[0]=reinterpret_cast<uint64_t>(allocation);
            Check(VW::Restore(redirects) && redirects.empty(),"owned retry can finish rollback");
        }
        {
            Fixture f;VW::Redirect redirect;std::string detail;
            bool mutated=false;
            PM::on_protect=[&](void* at,DWORD desired) {
                if(!mutated && at==&f.slots[0] && desired==PAGE_READWRITE) {mutated=true;f.slots[0]=0xbeef;}
            };
            Check(!VW::internal::CommitPreparedRedirect(f.plan,"race",redirect,detail),"commit must reject ownership lost during protect");
            Check(f.slots[0]==0xbeef,"commit must not overwrite foreign descriptor");
            PM::on_protect=nullptr;
            std::vector<VW::Redirect> redirects;redirects.push_back(std::move(redirect));
            if(redirects[0].allocation)f.slots[0]=reinterpret_cast<uint64_t>(redirects[0].allocation);
            Check(VW::Restore(redirects),"fixture recovery");
        }
        {
            Fixture f;VW::Redirect redirect;std::string detail;
            bool injected=false;
            PM::on_protect=[&](void* at,DWORD desired) {
                if(!injected && at==&f.slots[1] && desired==PAGE_READWRITE) {
                    injected=true;f.slots[0]=0xcafe;f.slots[1]=0xbeef;
                }
            };
            Check(!VW::internal::CommitPreparedRedirect(f.plan,"partial",redirect,detail),"partial commit fails");
            Check(f.slots[0]==0xcafe && f.slots[1]==0xbeef && redirect.allocation,
                "internal rollback preserves foreign descriptors and retains storage");
            PM::on_protect=nullptr;
            for(auto& slot:f.slots)slot=reinterpret_cast<uint64_t>(redirect.allocation);
            std::vector<VW::Redirect> redirects;redirects.push_back(std::move(redirect));
            Check(VW::Restore(redirects),"partial fixture recovery");
        }
        {
            unsigned char value=1;PM::regions.push_back({&value,1,MEM_IMAGE,PAGE_READONLY});
            mfgunlock::provider::internal::BytePatch patch{&value,1,2};
            bool mutated=false;
            PM::on_protect=[&](void* at,DWORD desired) {
                if(!mutated && at==&value && desired==PAGE_EXECUTE_READWRITE) {mutated=true;value=9;}
            };
            Check(!mfgunlock::provider::internal::WriteByte(patch,false),"byte commit race must refuse");
            Check(value==9,"byte commit preserves foreign value");
            PM::on_protect=nullptr;value=2;mutated=false;
            PM::on_protect=[&](void* at,DWORD desired) {
                if(!mutated && at==&value && desired==PAGE_EXECUTE_READWRITE) {mutated=true;value=8;}
            };
            Check(!mfgunlock::provider::internal::WriteByte(patch,true) && value==8,"byte rollback preserves foreign value");
            PM::on_protect=nullptr;std::erase_if(PM::regions,[&](auto r){return r.base==&value;});
        }
        Check(PM::allocations.empty(),"fixtures leave no allocations");
        std::puts("PASS: adversarial descriptor/byte commit and rollback ownership races");
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
