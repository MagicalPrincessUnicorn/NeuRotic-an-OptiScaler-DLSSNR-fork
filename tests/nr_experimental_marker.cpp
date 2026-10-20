#include "../OptiScaler/dlssnr/NrExperimentalPolicy.h"
#include <cstdio>
#include <string>
#include <thread>

struct CSimpleIniA { void SetBoolValue(const char*,const char*,bool) {} };
bool persistenceSuccess=true;
template<class Edit> bool SaveIniSubset(const std::string&,Edit&& edit) {
    CSimpleIniA file; edit(file); return persistenceSuccess;
}
struct Config {
    std::string absoluteFileName;
    NrOptional<bool> DlssNrExperimentalMode{false},DlssNrOverrideMultipassGuardrails{false},
        DlssNrOverrideHdrGuardrails{false},DlssNrOverrideFgGuardrails{false},
        DlssNrPreSrSoftReset{false},DlssNrPreparedDepth{false};
    bool SaveExperimentalSettings(bool,bool,bool,bool,bool=false);
};
#include "NrExperimentalWriterFixture.inc"
namespace DlssNr::ExperimentalSession {
namespace Detail {
    bool markerSuccess=true;
    void (*markerCallback)()=nullptr;
    std::atomic<bool> markerUnavailable{false};
    bool WriteMarker(bool) {
        if(markerCallback) markerCallback();
        return markerSuccess;
    }
}
// The already-initialized production UI path is selected by these fixtures.
void Initialize(Config*) {}
#include "NrExperimentalDisableFixture.inc"
#include "NrExperimentalAppliedFixture.inc"
}
Config* observedConfig=nullptr;
bool observedBeforeMarker=false;
bool ReadPrepared() {
    NrConfigSynchronization::Transaction transaction;
    return DlssNr::ExperimentalPolicy::Capture(*observedConfig).preparedDepth;
}
void CheckDuringMarker() {
    std::thread observer([] { observedBeforeMarker|=ReadPrepared(); });
    observer.join();
}
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 12; } } while(false)
int main(int argc,char**) {
    namespace Policy=DlssNr::ExperimentalPolicy;
    namespace Session=DlssNr::ExperimentalSession;
    Config config; observedConfig=&config;
    Policy::SessionReady.store(true);
    Policy::ResetDraft(config); Policy::Draft.preparedDepth=true;
    const auto save=[&] {
        return argc==1?config.SaveExperimentalSettings(false,false,false,false,true):
            Policy::SaveDraft(config,[](const Policy::UiDraft&) { return persistenceSuccess; });
    };
    // A failed settings write preserves the previously ready policy.
    persistenceSuccess=false;
    CHECK(!save()); CHECK(Policy::SessionReady.load()); CHECK(!ReadPrepared());
    persistenceSuccess=true;
    CHECK(save());
    std::thread observer([] { observedBeforeMarker=ReadPrepared(); }); observer.join();
    CHECK(!observedBeforeMarker); CHECK(!Policy::SessionReady.load());
    // The marker callback is the exact point before success publication.
    Session::Detail::markerCallback=CheckDuringMarker;
    CHECK(Session::Applied(config)); CHECK(!observedBeforeMarker);
    CHECK(Policy::SessionReady.load()); CHECK(ReadPrepared());
    // Failed removal persistence preserves the previously active depth policy.
    Policy::Draft.preparedDepth=false; persistenceSuccess=false;
    CHECK(!Policy::SaveDraft(config,[](const Policy::UiDraft&) { return persistenceSuccess; }));
    CHECK(Policy::SessionReady.load()); CHECK(ReadPrepared());
    // Successful persistence followed by marker refusal cannot activate depth.
    persistenceSuccess=true; Policy::Draft.preparedDepth=true;
    CHECK(save()); CHECK(!ReadPrepared());
    Session::Detail::markerSuccess=false;
    CHECK(!Session::Applied(config)); CHECK(!Policy::SessionReady.load());
    CHECK(!ReadPrepared()); CHECK(!Policy::Requested(config));
    std::puts(argc==1?"PASS: dedicated writer, pre-marker concurrent capture, persistence/marker refusal":
                       "PASS: global draft writer, pre-marker concurrent capture, persistence/marker refusal");
}
