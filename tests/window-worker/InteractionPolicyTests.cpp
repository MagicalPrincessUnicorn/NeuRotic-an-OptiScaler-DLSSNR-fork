#include "capture/InteractionPolicy.h"
#include <iostream>
int main(){int failed=0;auto check=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';failed+=!ok;};
 check(!nrw::InteractionBlockReason(true,true,false,true,0),"Available source continues without any foreground requirement");
 check(nrw::InteractionBlockReason(true,false,false,true,0)!=nullptr,"Disabled source remains blocked");
 check(nrw::InteractionBlockReason(true,true,true,true,0)!=nullptr,"Owned dialog remains unobscured");
 check(nrw::InteractionBlockReason(true,true,false,false,0)!=nullptr,"Unknown GUI state remains blocked");
 for(auto flag:{GUI_INMENUMODE,GUI_POPUPMENUMODE,GUI_SYSTEMMENUMODE,GUI_INMOVESIZE})check(nrw::InteractionBlockReason(true,true,false,true,flag)!=nullptr,"Menu and move/size keep safety pause");
 check(!nrw::InteractionBlockReason(false,false,true,false,GUI_INMENUMODE),"Separate preview retains independent interaction behavior");return failed?1:0;}
