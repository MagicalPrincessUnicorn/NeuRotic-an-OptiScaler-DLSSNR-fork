#include "../OptiScaler/dlssnr/NativeFgBridge.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static void Enter(bool){}
static void Leave(bool){}
int main(){
 using namespace DlssNr::NativeFg;
 auto dll=LoadLibraryW(L"builds/native-framegen/bridge/NeuRotic.Fsr3.Vulkan.dll");assert(dll);
 for(const auto name:{"NrFgInitialize","NrFgCreate","NrFgGetImages","NrFgAcquire","NrFgConfigure","NrFgPresent","NrFgDrain","NrFgDestroy","NrFgBindQueue","NrFgQueryProgress"})assert(GetProcAddress(dll,name));
 auto init=reinterpret_cast<BridgeInit>(GetProcAddress(dll,"NrFgInitialize"));
 auto drain=reinterpret_cast<BridgeDrain>(GetProcAddress(dll,"NrFgDrain"));
 auto destroy=reinterpret_cast<BridgeDestroy>(GetProcAddress(dll,"NrFgDestroy"));
 BridgeCallbacks callbacks{2,Enter,Leave};assert(!init(&callbacks));callbacks.version=3;assert(!init(&callbacks));callbacks.version=BridgeVersion;assert(init(&callbacks));
 auto bind=reinterpret_cast<BridgeBind>(GetProcAddress(dll,"NrFgBindQueue"));assert(!bind(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,0));
 assert(!drain(VK_NULL_HANDLE)&&destroy(VK_NULL_HANDLE,VK_NULL_HANDLE));
 FreeLibrary(dll);puts("PASS: packaged native FG DLL loads, ABI exports resolve, runtime mutex entry/exit succeeds (current CRT)");
}
