#include <PreparedFlowD3D12.h>
#include <PreparedFlowContract.h>
#include <PreparedFlowShader.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace Neurotic::PreparedFlow;
static int checks = 0;
static void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "Check %d failed\n", checks); std::exit(1); } }
int main()
{
    Check(Decode(16, -24, Backend::Software).x == 16);
    Check(Decode(16, -24, Backend::Software).y == -24);
    Check(Decode(64, -96, Backend::Nvidia).x == 2);
    Check(Decode(64, -96, Backend::Nvidia).y == -3);
    Check(Decode(-32768, 32767, Backend::Nvidia).x == -1024);
    Check(Grid(Backend::Software) == 8 && Grid(Backend::Nvidia) == 4);
    Check(GridExtent(1921, 8) == 241 && GridExtent(1921, 4) == 481);
    Check(!Completed(100, 0)); Check(!Completed(9, 10)); Check(Completed(10, 10));
    Check(!Completed(UINT64_MAX, 10));
    Check(Dimensions(1920, 1080)); Check(!Dimensions(0, 1080)); Check(!Dimensions(8192, 1080));
    Check(!Dimensions(64,64));
    Check(FiniteLuminance(0,1000)); Check(!FiniteLuminance(1,1));
    Check(!FiniteLuminance(0, std::numeric_limits<float>::infinity()));
    Check(!FiniteLuminance(std::numeric_limits<float>::quiet_NaN(),1000));
    Check(NvidiaCaps(1920,1080,256,4096,64,2160,true,true,true,true,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,4096,64,2160,false,true,true,true,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,4096,64,2160,true,false,true,true,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,4096,64,2160,true,true,false,true,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,4096,64,2160,true,true,true,false,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,4096,64,2160,true,true,true,true,Encoding::Pq));
    Check(!NvidiaCaps(1920,1080,0,4096,64,2160,true,true,true,true,Encoding::Srgb));
    Check(!NvidiaCaps(1920,1080,256,1000,64,2160,true,true,true,true,Encoding::Srgb));
    PairHistory history;
    Check(!history.Accepts({10,0,1,1},false)); Check(history.Accepts({10,0,1,1},true));
    history.Commit({10,0,1,1});
    Check(history.Accepts({11,10,1,1},false)); Check(history.Accepts({15,10,1,1},false));
    Check(!history.Accepts({10,0,1,1},true)); Check(!history.Accepts({9,8,1,1},false));
    Check(!history.Accepts({12,9,1,1},false)); Check(!history.Accepts({12,10,1,2},false));
    Check(!history.Accepts({12,10,2,1},false)); Check(history.Accepts({1,0,2,1},true));
    Check(!history.Accepts({12,12,1,1},false)); Check(!history.Accepts({12,10,0,1},false));
    Session session; std::string reason; Output output; Input input;
    Check(!session.Initialize(nullptr, {}, reason)); Check(!session.Submit(input, reason));
    Check(!session.Poll(output, reason)); Check(!session.AddReader({}, reason));
    Check(!session.ReturnOutput(reason)); Check(session.Close(reason));
    Neurotic::Feed::Prepared::Descriptor descriptor;
    Check(!ApplyCompletedFlow(output, descriptor));
    for (const auto* entry : {"Convert", "PrepareLuma"})
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        const auto status = D3DCompile(PreparedFlowShader, sizeof(PreparedFlowShader)-1, "PreparedFlowConvert.hlsl",
            nullptr, nullptr, entry, "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(status) && errors) std::fprintf(stderr, "%s\n", static_cast<const char*>(errors->GetBufferPointer()));
        Check(SUCCEEDED(status));
    }
    std::printf("prepared_flow_backends: %d CPU/negative/link checks passed; GPU/runtime not executed\n", checks);
}
