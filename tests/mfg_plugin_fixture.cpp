// Self-authored public test DLL, never a proprietary provider.
namespace sl::param {struct IParameters;}
static void (*probe)()=nullptr;
static bool originalResult=true;
extern "C" __declspec(dllexport) void FixtureSetProbe(void (*value)()) {probe=value;}
extern "C" __declspec(dllexport) void FixtureSetResult(bool value) {originalResult=value;}
extern "C" __declspec(dllexport) bool FixturePluginLoad(sl::param::IParameters*,const char*,const char** out)
{
    if(out)*out="{}";
    if(probe)probe();
    return originalResult;
}
