#include "../../../apps/NeuRoticHub/anything/AnythingPreferences.h"
#include <iostream>
int main(){nh::AnythingUiState state;auto legacy=nh::AnythingProfile(state);
 auto selected=legacy;selected["depthProvider"]="dav2";selected["depthProfile"]="fast";selected["depthHz"]=60;selected["depthPreview"]=true;
 if(!nh::ApplyAnythingProfile(state,selected)||nh::AnythingProfile(state).contains("depthProvider")||nh::AnythingProfile(state).contains("depthPreview")){std::cerr<<"FAIL old depth profile must migrate to image-only\n";return 1;}
 auto saved=nh::AnythingProfile(state);auto invalid=saved;invalid["depthPreview"]="yes";
 nh::AnythingUiState active;
 if(!nh::ApplyAnythingSessionProfile(active,selected,true)||nh::AnythingProfile(active)!=legacy){std::cerr<<"FAIL retired depth settings must not block look profiles\n";return 1;}
 auto same=legacy;same["detail"]=.5;
 if(!nh::ApplyAnythingSessionProfile(active,same,true)||active.transfer!=.5f||!nh::ApplyAnythingSessionProfile(active,selected,false)){std::cerr<<"FAIL allowed live look / stopped mode transition\n";return 1;}
 if(!nh::ApplyAnythingProfile(state,invalid)||nh::AnythingProfile(state)!=saved){std::cerr<<"FAIL retired settings affected valid look preferences\n";return 1;}
 if(!nh::ApplyAnythingProfile(state,legacy)||nh::AnythingProfile(state).contains("depthProvider")){std::cerr<<"FAIL legacy profile enabled depth\n";return 1;}
 std::cout<<"PASS stale depth preferences migrate to image-only and retain look settings\n";return 0;}
