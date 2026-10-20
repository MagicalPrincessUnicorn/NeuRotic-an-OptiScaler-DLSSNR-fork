#define NR_PREPARED_GUIDES_NO_WINDOWS_QUERY
#include "../OptiScaler/dlssnr/PreparedGuideStatus.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace DlssNr::PreparedGuides;
int main() {
    Status status;
    status.session=7;status.capture=23;status.updatedTickMs=1000;
    status.active=1;status.depthReady=1;status.motionReady=1;status.stage=Stage::Delivered;
    status.inputFrames=20;status.modelCompletions=15;status.copybackCompletions=12;
    auto current=ValidateStatus(status,4000);
    assert(current.available && current.fresh && current.status.depthReady && current.status.motionReady);
    assert(current.status.modelCompletions==15 && current.status.copybackCompletions==12);
    auto stale=ValidateStatus(status,4001);
    assert(stale.available && !stale.fresh && !stale.status.active && !stale.status.depthReady && !stale.status.motionReady);
    assert(stale.status.stage!=Stage::Delivered && stale.status.modelCompletions==15);
    assert(!ValidateStatus(status,999).fresh);
    status.version=2;assert(!ValidateStatus(status,1000).available);status.version=1;
    status.size=sizeof(Status)-1;assert(!ValidateStatus(status,1000).available);status.size=sizeof(Status);
    status.stage=static_cast<Stage>(99);assert(!ValidateStatus(status,1000).available);status.stage=Stage::Delivered;
    status.active=0;current=ValidateStatus(status,1000);
    assert(current.fresh && !current.status.depthReady && !current.status.motionReady && current.status.stage!=Stage::Delivered);
    status.active=1;status.capture=0;current=ValidateStatus(status,1000);
    assert(!current.status.depthReady && !current.status.motionReady);status.capture=23;
    status.session=0;current=ValidateStatus(status,1000);
    assert(!current.status.depthReady && !current.status.motionReady);status.session=7;
    std::memset(status.reason,'a',sizeof(status.reason));current=ValidateStatus(status,1000);
    assert(std::strlen(current.status.reason)==159);
    status.reason[1]='\n';current=ValidateStatus(status,1000);assert(current.status.reason[1]==' ');
    assert(!QueryStatus().available);
    std::cout << "prepared status freshness, ABI admission and bounded text passed\n";
}
