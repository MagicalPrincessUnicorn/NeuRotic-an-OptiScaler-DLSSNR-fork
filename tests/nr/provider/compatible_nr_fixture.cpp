// Local export fixture. The test only probes symbols; it never calls these
// functions or enters a provider, device, queue or game.
extern "C" {
__declspec(dllexport) int NVSDK_NGX_D3D12_Init_Ext() { return 1; }
__declspec(dllexport) int NVSDK_NGX_D3D12_CreateFeature() { return 1; }
__declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature() { return 1; }
__declspec(dllexport) int NVSDK_NGX_D3D12_ReleaseFeature() { return 1; }
__declspec(dllexport) int NVSDK_NGX_D3D12_Shutdown() { return 1; }
}
