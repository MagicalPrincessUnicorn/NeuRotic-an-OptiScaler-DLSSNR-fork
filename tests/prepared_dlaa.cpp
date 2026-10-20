// Compile the production implementation and its exact conversion shaders. This
// test deliberately initializes no GPU, NGX runtime or model.
#include "../addons/prepared-guides/helper/DlaaHost.cpp"
#include <cassert>
int main()
{
    nrpg::DlaaHost host;std::string reason;
    assert(!host.Initialize(nullptr,nullptr,L"relative.dll",reason) && !reason.empty());
    assert(!host.RequiresRestart());
    assert(!host.Process(nullptr,nullptr,nullptr,1920,1080,true,false,1,1,reason));
    assert(!host.RequiresRestart());host.Stop();
    Microsoft::WRL::ComPtr<ID3DBlob> code,error;
    assert(SUCCEEDED(D3DCompile(nrpg::BlitSource,sizeof(nrpg::BlitSource)-1,nullptr,nullptr,nullptr,
        "vs","vs_5_0",D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error)));
    code.Reset();error.Reset();
    assert(SUCCEEDED(D3DCompile(nrpg::BlitSource,sizeof(nrpg::BlitSource)-1,nullptr,nullptr,nullptr,
        "ps","ps_5_0",D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error)));
}
