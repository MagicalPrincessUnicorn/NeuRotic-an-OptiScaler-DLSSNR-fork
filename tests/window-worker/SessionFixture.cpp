// Owned benign external window for the connected worker test. GPL-3.0.
#include <Windows.h>
#include <json.hpp>
#include <string>
#include <iostream>
#include <algorithm>
namespace {
unsigned tick=0;uint64_t deadline=0;
LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);
        auto dark=CreateSolidBrush(RGB(25,35,52)),light=CreateSolidBrush(RGB(230,195,125));
        FillRect(dc,&r,dark);
        for(int y=0;y<r.bottom;y+=32)for(int x=-32;x<r.right;x+=32)if(((x/32+y/32)&1)==0){RECT tile{x+int(tick%16),y,x+int(tick%16)+28,y+28};FillRect(dc,&tile,light);}
        DeleteObject(dark);DeleteObject(light);EndPaint(window,&ps);return 0;
    }
    if(message==WM_TIMER){++tick;InvalidateRect(window,nullptr,FALSE);if(GetTickCount64()>=deadline)DestroyWindow(window);return 0;}
    if(message==WM_APP+1){SetWindowPos(window,nullptr,0,0,LOWORD(wp),HIWORD(wp),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_APP+2){SetForegroundWindow(window);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    unsigned width=1280,height=720;std::wstring title=L"NeuRotic owned pipeline fixture";
    if(argc>1)title=argv[1];
    WNDCLASSW cls{};cls.lpfnWndProc=FixtureProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NRWSessionSourceFixture";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&cls);deadline=GetTickCount64()+180000;
    auto hwnd=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,title.c_str(),WS_POPUP|WS_VISIBLE,40,40,width,height,nullptr,nullptr,cls.hInstance,nullptr);
    if(!hwnd)return 1;
    UpdateWindow(hwnd);SetTimer(hwnd,1,50,nullptr);
    FILETIME creation{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);
    nlohmann::json identity={{"hwnd",reinterpret_cast<uint64_t>(hwnd)},{"pid",GetCurrentProcessId()},
        {"processCreation",(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime},
        {"title","NeuRotic owned pipeline fixture"},{"width",width},{"height",height}};
    std::cout<<identity.dump()<<std::endl;
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
