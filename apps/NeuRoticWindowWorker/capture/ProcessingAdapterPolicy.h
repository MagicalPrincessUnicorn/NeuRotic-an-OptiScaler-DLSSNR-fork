// GPL-3.0. Policy uses observed ordering/identity and actual API outcomes only.
#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include <algorithm>
namespace nrw::adapters {
struct Identity {std::uint32_t low=0;std::int32_t high=0;bool known=false;bool operator==(const Identity&)const=default;};
struct Candidate {Identity id;bool software=false;std::int32_t identityFailure=0;};
struct ProbeResult {bool supported=false,closed=true,opaqueEntered=false;std::int32_t code=0;std::string phase;};
struct Attempt {Identity id;ProbeResult result;};
inline constexpr std::size_t NoCandidate=static_cast<std::size_t>(-1);
struct Selection {std::size_t index=NoCandidate;bool quarantined=false;std::vector<Attempt> attempts;};
template<class Probe>Selection Choose(std::span<const Candidate> candidates,Probe&& probe){
    Selection selection;std::vector<Identity> seen;
    for(std::size_t i=0;i<candidates.size();++i){
        const auto& candidate=candidates[i];
        if(candidate.software)continue;
        if(!candidate.id.known){selection.attempts.push_back({candidate.id,{false,true,false,candidate.identityFailure,"adapter-identity"}});continue;}
        if(std::find(seen.begin(),seen.end(),candidate.id)!=seen.end())continue;
        seen.push_back(candidate.id);
        auto result=probe(i);selection.attempts.push_back({candidate.id,result});
        // This policy is only for pre-provider creation. Never repeat uncertain
        // work or turn an opaque provider call into a hardware probe.
        if(!result.closed||result.opaqueEntered){selection.quarantined=true;return selection;}
        if(result.supported){selection.index=i;return selection;}
    }
    return selection;
}
inline bool PinnedMatch(Identity selected,Identity actual,std::uint64_t session,std::uint64_t actualSession){
    return selected.known&&actual.known&&selected==actual&&session&&session==actualSession;
}
}
