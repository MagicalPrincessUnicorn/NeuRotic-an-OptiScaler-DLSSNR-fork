#pragma once
#include <storage/NrDllProfiles.h>
#include <json.hpp>
#include <algorithm>

namespace nh {
enum class NrDllChoiceMode { Default, Manual, Profile };
struct NrDllGameChoice { NrDllChoiceMode mode=NrDllChoiceMode::Default;std::string profileId; };
enum class NrDllTargetState { Initial, Existing, Uncertain };
enum class NrDllCopyMode { Preserve, None, Profile };
struct NrDllCopySnapshot { NrDllCopyMode mode=NrDllCopyMode::None;NrDllProfile profile; };
class NrDllChoiceError : public std::runtime_error {
public:
 explicit NrDllChoiceError(NrDllErrorCode value):std::runtime_error("nr_dll_choice_unavailable"),code(value){}
 const NrDllErrorCode code;
};
// The storage catalog uses an empty profileId for a default-sidecar error.
// Keep unreadable/unsafe default metadata distinct from legitimate no-default.
inline std::optional<NrDllErrorCode> NrDllDefaultIssue(const NrDllCatalog& catalog){
 for(const auto& issue:catalog.issues)if(issue.profileId.empty())return issue.code;
 return std::nullopt;
}
inline NrDllCopySnapshot ResolveNrDllSelection(const NrDllGameChoice& choice,const NrDllCatalog& catalog,bool required){
 if(choice.mode==NrDllChoiceMode::Manual){if(required)throw NrDllChoiceError(NrDllErrorCode::MissingProfile);return {};}
 if(choice.mode==NrDllChoiceMode::Default)if(const auto issue=NrDllDefaultIssue(catalog))throw NrDllChoiceError(*issue);
 const auto& id=choice.mode==NrDllChoiceMode::Profile?choice.profileId:catalog.defaultId;
 if(id.empty()&&choice.mode==NrDllChoiceMode::Default){if(required)throw NrDllChoiceError(NrDllErrorCode::MissingProfile);return {};}
 const auto found=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});
 if(found==catalog.profiles.end()||!found->available)throw NrDllChoiceError(NrDllErrorCode::MissingProfile);
 return {NrDllCopyMode::Profile,*found};
}
// Ordinary Install/Update/Fresh retains its existing preservation policy.
inline NrDllCopySnapshot ResolveNrDllChoice(const NrDllGameChoice& choice,const NrDllCatalog& catalog,NrDllTargetState target){
 if(target!=NrDllTargetState::Initial)return {NrDllCopyMode::Preserve,{}};
 return ResolveNrDllSelection(choice,catalog,false);
}
// An explicit copy is its own operation; never impersonate an initial install.
inline NrDllCopySnapshot ResolveNrDllReplacement(const NrDllGameChoice& choice,const NrDllCatalog& catalog){
 return ResolveNrDllSelection(choice,catalog,true);
}
inline nlohmann::json NrDllCopyRequest(const NrDllCopySnapshot& snapshot){
 if(snapshot.mode!=NrDllCopyMode::Profile)return {{"mode",snapshot.mode==NrDllCopyMode::Preserve?"preserve":"none"}};
 const auto path=snapshot.profile.dllPath.u8string();
 return {{"mode","profile"},{"profileId",snapshot.profile.id},{"profileName",snapshot.profile.name},
         {"source",std::string(path.begin(),path.end())},{"sha256",snapshot.profile.sha256},{"bytes",snapshot.profile.bytes}};
}
inline bool NrDllAcquisitionMatches(const NrDllCopySnapshot& snapshot,const NrDllSelection& acquired){
 // A real non-null lease is a separate queue execution requirement. This
 // comparison validates continuity of the chosen asset, never its admission.
 return snapshot.mode==NrDllCopyMode::Profile&&snapshot.profile.id==acquired.profile.id&&
        snapshot.profile.dllPath==acquired.profile.dllPath&&snapshot.profile.sha256==acquired.profile.sha256&&
        snapshot.profile.bytes==acquired.profile.bytes;
}
}
