"""Execute the production child-creation loop with controlled provider outcomes.

The proprietary Create operation is the only opaque dependency. Extract the real
loop so this regression exercises its return/break behavior rather than a copy.
Compile in the caller's VS x64 environment. No provider, game or GPU is launched.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--out-dir', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
start = source.index('    // Later features are created one per lifecycle-only command list.')
end = source.index('    const bool secondLayerActive', start)
loop = source[start:end]
gate_start = source.index('    const bool releasedLayer2ThisCall = TickNrRetired(true);')
gate_end = source.index('    // Placement belongs to the current opaque feature generation.', gate_start)
gate = source[gate_start:gate_end]
primary_start = source.index('    if (g_nr.feature != nullptr && (resolutionChanged || tuningChanged))')
primary_end = source.index('    if (g_nr.meter == nullptr)', primary_start)
primary = source[primary_start:primary_end]
second_start = source.index('    if (secondLayerHealthy && g_nr.layer2.feature != nullptr &&')
second_end = source.index('    if (secondLayerHealthy && g_nr.layer2.feature == nullptr)', second_start)
second = source[second_start:second_end]
out = args.out_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
fixture = out / 'creation.cpp'
fixture.write_text(r'''
#include <array>
#include <filesystem>
#include <optional>
#include <cstdio>
#include <cstdlib>
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
struct Layer {
    void* feature = nullptr;
    void* featureAwaitingRelease = nullptr;
    unsigned width=1920,height=1080,workWidth=1280,workHeight=720;
    bool failed=false,reset=false,ready=false,routeKnown=false,routeWasPreSr=false;
    const char* reason="";
};
std::array<Layer,9> layers;
unsigned g_layer2FeatureBuilds=0, creates=0, releases=0, evaluations=0;
bool missing=false, refuse=false, preparation=false, tuningMatches=true;
bool g_shutdownFailed=false, completion=true, releaseFailed=false;
struct {
    void* feature=nullptr;
    void* resumeFeatureAwaitingRelease=nullptr;
    void* preSrScratch=nullptr;
    void* preSrRejitter=nullptr;
    bool preSrScratchPrimed=false,preSrAwaitingEvaluation=false;
    unsigned workWidth=1280,workHeight=720;
    Layer layer2;
} g_nr;
unsigned g_featureRebuilds=0;
std::filesystem::path g_dllDir;
Layer& AdditionalLayer(size_t i) { return layers.at(i); }
void ParkAdditionalLayerFeature(size_t i, const char*) {
    auto& l=layers.at(i); l.featureAwaitingRelease=l.feature; l.feature=nullptr;
}
void ParkNrFeature(void*& feature) { feature=nullptr; }
void ParkNrResource(void*& resource) { resource=nullptr; }
void ParkAllAdditionalLayerFeatures(const char* reason) {
    for(size_t i=0;i<layers.size();++i) ParkAdditionalLayerFeature(i,reason);
}
void ParkSecondLayerFeature(const char*) {
    g_nr.layer2.featureAwaitingRelease=g_nr.layer2.feature; g_nr.layer2.feature=nullptr;
}
bool SecondLayerTuningMatches(int) { return tuningMatches; }
struct Scratch { template<class F> void Commit(F) {} };
bool TickNrRetired(bool) {
    if(!completion) return false;
    if(releaseFailed) { g_shutdownFailed=true; return false; }
    g_nr.resumeFeatureAwaitingRelease=nullptr;
    bool released=false;
    for(auto& l:layers) if(l.featureAwaitingRelease) {
        l.featureAwaitingRelease=nullptr; released=true;
    }
    return released;
}
void ReportSkipOnce(const char*) {}
template<class C> bool AdditionalLayerTuningMatches(const C&,size_t) { return tuningMatches; }
template<class C> void RecordAdditionalLayerTuning(const C&,size_t) { tuningMatches=true; }
template<class... A> void SetExtras(A...) {}
struct Settings { int tuning=0; };
template<class C> Settings PassSettings(const C&,unsigned) { return {}; }
struct Device { void Release() { ++releases; } } deviceObject;
namespace Util {
std::optional<std::filesystem::path> FindFilePath(const std::filesystem::path&,const char*) {
    return missing ? std::nullopt : std::optional<std::filesystem::path>("controlled-provider");
}
std::filesystem::path ExePath() { return "fixture.exe"; }
}
void* CreateNrFeature(int,Device*,void*,const std::filesystem::path&,unsigned,unsigned) {
    ++creates; return refuse ? nullptr : reinterpret_cast<void*>(2);
}
void Run(unsigned healthyPassCount) {
    preparation=false;
    bool* preparationRecorded=&preparation;
    Device* device=&deviceObject;
    void* cmdList=nullptr;
    int cfg=0;
    unsigned width=1920,height=1080;
    const bool currentRouteIsPreSr=false;
    std::array<unsigned,9> additionalWorkWidth,additionalWorkHeight;
    additionalWorkWidth.fill(1280); additionalWorkHeight.fill(720);
''' + gate + loop + r'''
    // The next effectful stage is model evaluation. Reaching it after an
    // attempted opaque creation is precisely the unsafe behavior under test.
    ++evaluations;
    device->Release();
}
void Check(bool ok,const char* message) {
    if (!ok) { std::printf("FAIL: %s\n",message); std::exit(1); }
}
void RunPrimary(bool resolutionChanged,bool tuningChanged,bool frameDimensionsChanged=false) {
    preparation=false; bool* preparationRecorded=&preparation;
    Device* device=&deviceObject; void* target=reinterpret_cast<void*>(3);
    Scratch scratch; Scratch* layer2Scratch=nullptr; std::array<Scratch*,9> laterScratch{};
    unsigned workWidth=1280,workHeight=720;
''' + primary + r'''
    ++evaluations;
}
void RunSecond() {
    preparation=false; bool* preparationRecorded=&preparation;
    Device* device=&deviceObject; int cfg=0; bool secondLayerHealthy=true;
    unsigned width=1920,height=1080,layer2WorkWidth=1280,layer2WorkHeight=720;
''' + second + r'''
    ++evaluations;
}
int main() {
    for(unsigned count=3;count<=10;++count) {
        for(unsigned outcome=0;outcome<3;++outcome) {
            creates=releases=evaluations=g_layer2FeatureBuilds=0;
            missing=outcome==2; refuse=outcome==1;
            for(auto& layer:layers) { layer={}; layer.feature=reinterpret_cast<void*>(1); }
            layers[count-2].feature=nullptr;
            Run(count);
            Check(releases==1,"device reference released once");
            if(missing) {
                Check(creates==0 && evaluations==1,"missing provider can use existing prefix without Create");
                Check(!preparation,"missing provider never certifies initialization work");
            } else {
                Check(creates==1,"one opaque creation attempt per list");
                Check(evaluations==0,"opaque Create success OR failure must be lifecycle-only");
                Check(g_layer2FeatureBuilds==(refuse?0u:1u),"only successful creations advance generation");
                Check(preparation==!refuse,"only successful creation permits a sealed-completion preparation retry");
            }
        }
    }
    for(unsigned count=3;count<=10;++count) for(unsigned changed=0;changed<2;++changed) {
        creates=releases=evaluations=g_layer2FeatureBuilds=0;
        missing=refuse=g_shutdownFailed=releaseFailed=false; completion=true; tuningMatches=true;
        for(auto& layer:layers) { layer={}; layer.feature=reinterpret_cast<void*>(1); }
        // A working-size change retires this child; a tuning change retires the first child.
        if(changed==0) layers[count-2].workWidth=640; else tuningMatches=false;
        Run(count);
        Check(preparation && evaluations==0 && creates==0,"signature change retires without evaluation and permits retry");
        completion=false; Run(count);
        Check(preparation && evaluations==0 && creates==0,"pending known retirement remains retryable without bypassing release");
        completion=true; Run(count);
        Check(preparation && evaluations==0 && creates==0,"successful child release is a separate preparation boundary");
        Run(count);
        Check(preparation && evaluations==0 && creates==1,"replacement creation is isolated from evaluation");
        Run(count);
        Check(!preparation && evaluations==1 && creates==1,"replacement evaluates only after release and creation boundaries");
        auto& l=layers[count-2]; l.workWidth=640; Run(count);
        releaseFailed=true; Run(count);
        Check(!preparation && evaluations==1 && g_shutdownFailed,"failed release never permits a preparation retry");
    }
    for(unsigned changed=0;changed<2;++changed) {
        evaluations=0; g_shutdownFailed=releaseFailed=false; completion=true;
        for(auto& layer:layers) { layer={}; layer.feature=reinterpret_cast<void*>(1); }
        g_nr.feature=reinterpret_cast<void*>(1); g_nr.resumeFeatureAwaitingRelease=nullptr;
        RunPrimary(changed==0,changed==1);
        Check(preparation && evaluations==0 && !g_nr.feature && g_nr.resumeFeatureAwaitingRelease,
            "primary resize/tuning parks exact feature and permits completion-gated retry");
        completion=false; Run(3);
        Check(preparation && evaluations==0 && g_nr.resumeFeatureAwaitingRelease,
            "primary pending retirement cannot evaluate or invent release proof");
        releaseFailed=true; completion=true; Run(3);
        Check(!preparation && evaluations==0 && g_nr.resumeFeatureAwaitingRelease,
            "primary failed release retains exact feature and refuses retry");
        g_nr.resumeFeatureAwaitingRelease=nullptr;
        g_nr.layer2={}; g_nr.layer2.feature=reinterpret_cast<void*>(1); tuningMatches=changed==0;
        if(changed==0) g_nr.layer2.workWidth=640;
        RunSecond();
        Check(preparation && evaluations==0 && !g_nr.layer2.feature && g_nr.layer2.featureAwaitingRelease,
            "second pass resize/tuning retirement is retryable without output");
    }
    std::puts("PASS: production lifecycle branches: 24 provider outcomes; 16 child resize/tuning retire-wait-release-create-evaluate sequences; primary/second-pass resize+tuning retirement; failed release refused (controlled provider/completion)");
}
''', encoding='utf-8')
result = subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', str(fixture),
                         '/Fe:'+str(out/'creation.exe'), '/Fo:'+str(out/'creation.obj')])
if result.returncode == 0:
    result = subprocess.run([str(out/'creation.exe')])
raise SystemExit(result.returncode)
