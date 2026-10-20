#include <Windows.h>
#include <cstdio>
int main(){
    auto module=LoadLibraryW(L"lifecycle-plugin.dll");if(!module){std::puts("FAIL load");return 1;}
    auto ready=reinterpret_cast<LONG(*)()>(GetProcAddress(module,"Ready"));if(!ready)return 2;
    FreeLibrary(module);const auto deadline=GetTickCount64()+5000;
    while(ready()==0&&GetTickCount64()<deadline)Sleep(1);
    const bool pinned=GetModuleHandleW(L"lifecycle-plugin.dll")==module,once=ready()==1;
    for(int i=0;i<4;++i){auto again=LoadLibraryW(L"lifecycle-plugin.dll");if(!again)return 3;FreeLibrary(again);}
    const bool stillOnce=ready()==1;
    std::printf("Runtime pin/worker lifecycle: 3 checks, %d failures\n",(!pinned)+(!once)+(!stillOnce));
    return pinned&&once&&stillOnce?0:1;
}
