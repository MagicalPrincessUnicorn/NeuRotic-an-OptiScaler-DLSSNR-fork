#include <dlssnr/connections/ConnectionPolicy.h>
#include <Windows.h>
#include <cassert>
#include <cstdio>
using namespace DlssNr::Connections;
int main(){
    SignalPolicyWire policy;assert(NeuRotic_QuerySignalPolicyV1(&policy,sizeof(policy))&&policy.depth==0&&policy.motion==0&&!policy.strict);
    assert(!NeuRotic_QuerySignalPolicyV1(&policy,sizeof(policy)-1));
    assert(RequestSignalPreferences(3,4,true));assert(NeuRotic_QuerySignalPolicyV1(&policy,sizeof(policy))&&policy.depth==3&&policy.motion==4&&policy.strict);
    assert(!RequestSignalPreferences(8,4,false));
    assert(NeuRotic_ClaimPreparedConnectionV1(3,1,123));
    SignalStatusWire status;status.capture=2;status.generation=1;status.revision=policy.revision;
    status.depth=3;status.motion=4;status.consumed=1;
    assert(!NeuRotic_PublishInputSignalsV1(3,124,&status,sizeof(status)));
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status))); // no matching admission
    status.consumed=0;assert(NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status)));
    status.consumed=1;
    assert(NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status)));
    assert(QueryInputSignals(GetTickCount64()).fresh);
    auto plan=status;plan.capture=3;plan.depth=2;plan.consumed=0;
    assert(NeuRotic_PublishInputSignalsV1(3,123,&plan,sizeof(plan)));
    const auto separated=QueryInputSignals(GetTickCount64());
    assert(separated.status.depth==2&&separated.used.depth==3&&separated.usedFresh);
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status))); // late older capture
    InvalidateSignalReceipts();
    plan.consumed=1;
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&plan,sizeof(plan))); // off/resume invalidated admission
    plan.consumed=0;
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&plan,sizeof(plan))); // late producer from old runtime epoch
    assert(!QueryInputSignals(GetTickCount64()).usedFresh);
    assert(!QueryInputSignals(GetTickCount64()+501).fresh);
    assert(RequestSignalPreferences(0,0,false));assert(!QueryInputSignals(GetTickCount64()).fresh);
    auto invalid=status;invalid.depth=0;
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&invalid,sizeof(invalid)));
    status.revision=SignalPreferences().revision;
    status.consumed=0;status.capture=4;
    assert(NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status)));
    status.consumed=1;
    assert(NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status)));
    assert(NeuRotic_RetirePreparedConnectionV1(3,123,1));
    assert(!QueryInputSignals(GetTickCount64()).fresh);
    assert(!QueryInputSignals(GetTickCount64()).usedFresh);
    assert(QueryCompletedInputSignals(GetTickCount64()).usedFresh);
    assert(!QueryCompletedInputSignals(GetTickCount64()+501).usedFresh);
    assert(!NeuRotic_PublishInputSignalsV1(3,123,&status,sizeof(status)));
    assert(RequestSavedSignalPreferences(99,2,true));
    assert(!QueryCompletedInputSignals(GetTickCount64()).usedFresh);
    assert(uint32_t(SignalPreferences().depth)==7&&SignalPreferences().motion==Source::BuiltIn&&SignalPreferences().strict);
    assert(RequestSavedSignalPreferences(5,0,false));
    assert(NeuRotic_QuerySignalPolicyV1(&policy,sizeof(policy))&&policy.depth==5&&policy.motion==0&&!policy.strict);
    assert(!SignalPreferences().strict); // Soft None preserves fallback for legacy readers too.
    // V2 color remains independent; V1 cannot silently ignore new color intent.
    assert(RequestSignalPreferences(3,4,false,1));
    SignalPolicyWireV2 colorPolicy;
    assert(NeuRotic_QuerySignalPolicyV2(&colorPolicy,sizeof(colorPolicy))&&colorPolicy.color==1&&colorPolicy.depth==3&&colorPolicy.motion==4&&!colorPolicy.strict);
    assert(!NeuRotic_QuerySignalPolicyV2(&colorPolicy,sizeof(colorPolicy)-1));
    assert(NeuRotic_QuerySignalPolicyV1(&policy,sizeof(policy))&&policy.depth==7&&policy.strict);
    assert(NeuRotic_ClaimPreparedConnectionV1(3,1,456));
    SignalStatusWireV2 colorStatus;colorStatus.capture=9;colorStatus.generation=2;colorStatus.revision=colorPolicy.revision;
    colorStatus.color=1;colorStatus.depth=3;colorStatus.motion=4;
    assert(NeuRotic_PublishInputSignalsV2(3,456,&colorStatus,sizeof(colorStatus)));
    auto wrongColor=colorStatus;wrongColor.color=2;wrongColor.consumed=1;
    assert(!NeuRotic_PublishInputSignalsV2(3,456,&wrongColor,sizeof(wrongColor)));
    colorStatus.consumed=1;assert(NeuRotic_PublishInputSignalsV2(3,456,&colorStatus,sizeof(colorStatus)));
    auto usedColor=QueryInputSignals(GetTickCount64());assert(usedColor.usedFresh&&usedColor.used.color==1);
    colorStatus.capture=10;colorStatus.consumed=0;
    assert(NeuRotic_PublishInputSignalsV2(3,456,&colorStatus,sizeof(colorStatus)));
    InvalidateSignalReceipts();colorStatus.consumed=1;
    assert(!NeuRotic_PublishInputSignalsV2(3,456,&colorStatus,sizeof(colorStatus))&&!QueryInputSignals(GetTickCount64()).usedFresh);
    assert(NeuRotic_RetirePreparedConnectionV1(3,456,1));
    assert(RequestSavedSignalPreferences(3,4,true,99)&&uint32_t(SignalPreferences().color)==7);
    std::puts("PASS V1/V2 color policy, paired consumer, revocation, legacy refusal, expiry and ownership");
}
