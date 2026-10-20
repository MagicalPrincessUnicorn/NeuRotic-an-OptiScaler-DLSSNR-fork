#include "nr/semantic/character/CharacterInstallLayout.h"
#include <cassert>
#include <iostream>
int main(){
 using namespace Neurotic::Semantic::Character;
 assert(std::wstring(RequiredCharacterInspectorVersion)==L"Version 1");
 assert(CharacterInspectorRootForModule(L"C:/Games/Fixture/winmm.dll")==std::filesystem::path(L"C:/Games/Fixture/OptiScaler/CharacterInspector/Version 1"));
 assert(CharacterInspectorRootForModule(L"D:/Moved/Fixture/dxgi.dll")==std::filesystem::path(L"D:/Moved/Fixture/OptiScaler/CharacterInspector/Version 1"));
 assert(CharacterInspectorRootForModule(L"C:/Games/Fixture/NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.dll")==std::filesystem::path(L"C:/Games/Fixture/OptiScaler/CharacterInspector/Version 1"));
 std::cout<<"PASS module-relative required version, moved game, legacy helper resolves game root\n";
}
