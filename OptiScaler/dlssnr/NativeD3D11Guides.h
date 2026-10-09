#pragma once
#include <cstdint>
#include <string>
#include <d3d11.h>
#include <dxgi.h>
namespace DlssNr::NativeD3D11Guides {
void HookDevice(ID3D11Device*);
void Present(IDXGISwapChain*,ID3D11Device*,bool nrEnabled);
// Caller must first gate new rendering and finish in-flight Present callbacks.
// Read-only: never waits, retires, or treats a timeout as GPU completion.
bool CanYieldOutput(std::string& reason);
}

#ifdef NR_D3D11_OBSERVER_TEST
namespace DlssNr::NativeD3D11Guides {
void TestRefuseSerialization(ID3D11DeviceContext*);
void TestRefuseCompletion(ID3D11DeviceContext*);
void TestFailNextHookCommit();
bool TestCaptureEnabled();
bool TestContextRegistered(ID3D11DeviceContext*);
bool TestPresentContext(ID3D11DeviceContext*);
bool TestObservedDepth(ID3D11DeviceContext*,unsigned,unsigned,ID3D11Texture2D**,int&);
void TestConsumeInterval(ID3D11DeviceContext*);
bool TestSourceReset(ID3D11Texture2D*,unsigned,unsigned,int,uint64_t&);
void TestAcceptHistory(bool);
void TestSetCaptureActive(bool);
uint64_t TestSnapshotBytes();
}
#endif
