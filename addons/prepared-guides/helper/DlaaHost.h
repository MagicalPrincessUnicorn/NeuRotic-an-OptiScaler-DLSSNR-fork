#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
namespace nrpg
{
// Optional helper-owned public DLSS feature. No game NGX singleton is initialized
// or shut down here. Inputs retain their prepared/estimated provenance.
class DlaaHost
{
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    DlaaHost();~DlaaHost();
    bool Initialize(ID3D11Device*,ID3D11DeviceContext*,const std::wstring& ngxPath,std::string& reason);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> Process(ID3D11Texture2D* color,ID3D11Texture2D* depth,
        ID3D11Texture2D* motion,UINT width,UINT height,bool reset,bool reversed,float scaleX,float scaleY,
        std::string& reason,ID3D11Texture2D* distrust=nullptr);
    bool RequiresRestart()const;
    void Stop();
};
}
