// Reuse owned source/polling helpers; production capture implementation is unchanged.
#define wmain ExistingCaptureFixtureMain
#include "CaptureTests.cpp"
#undef wmain
#include "../../apps/NeuRoticWindowWorker/nr/NrPolicy.h"
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && std::wstring(argv[1])==L"--fixture")return Fixture(argv[2]);
    PROCESS_INFORMATION child{};HWND source=nullptr;int code=0;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        auto title=L"NRW adversarial source "+std::to_wstring(GetCurrentProcessId());
        auto command=L"\""+std::wstring(exe)+L"\" --fixture \""+title+L"\"";
        STARTUPINFOW si{sizeof(si)};
        Check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&child)!=FALSE,"create owned audit source");
        auto deadline=GetTickCount64()+5000;
        while(!source && GetTickCount64()<deadline){source=FindWindowW(L"NRWOwnedFixture",title.c_str());Sleep(10);}
        Check(source!=nullptr,"owned audit source appears");
        nrw::WindowIdentity id;std::string reason;Check(nrw::CaptureHost::Inspect(source,id,reason),"source identity");
        nrw::CaptureHost host;nrw::CaptureOptions options{id,89,false};Check(StartCaptureSession(host,options,reason),"actual WGC start");
        auto first=WaitFrame(host);auto stable=WaitFrame(host);
        Sleep(2000);auto delayed=WaitFrame(host);
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        auto gap=1000.*double(delayed.stamp.timestampQpc-stable.stamp.timestampQpc)/frequency.QuadPart;
        std::cout<<"actual_capture_gap_ms="<<gap<<" previous_sequence="<<stable.stamp.sequence<<" next_sequence="<<delayed.stamp.sequence
          <<" capture_reset="<<delayed.stamp.reset<<" NR_reset="<<nrw::detail::ResetHistory(delayed.stamp,stable.stamp)<<'\n';
        Check(host.Present(delayed,nullptr,1,reason),"initial client-sized output present");
        HWND output=FindWindowW(L"NeuRoticWindowWorkerOutput",nullptr);Check(output!=nullptr,"owned output exists");
        RECT square{0,0,300,300};AdjustWindowRect(&square,WS_OVERLAPPEDWINDOW,FALSE);
        SetWindowPos(output,nullptr,0,0,square.right-square.left,square.bottom-square.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(host.Pump(),"output resize remains active");auto frame=WaitFrame(host);
        Check(host.Present(frame,nullptr,1,reason),"resized output original present");
        RECT actual{};GetClientRect(output,&actual);
        std::cout<<"source_raster="<<frame.stamp.width<<'x'<<frame.stamp.height<<" displayed_client="<<actual.right<<'x'<<actual.bottom<<" swapchain_policy=STRETCH\n";
        ShowWindow(source,SW_MINIMIZE);Sleep(100);bool whileMinimized=host.Pump();
        ShowWindow(source,SW_RESTORE);Sleep(100);bool restored=host.Pump();
        std::cout<<"source_still_exists="<<(IsWindow(source)!=FALSE)<<" pump_minimized="<<whileMinimized<<" pump_after_restore="<<restored<<'\n';
        host.Stop();Check(host.Device()==nullptr,"normal owned capture teardown");
    } catch(const std::exception& e){std::cerr<<"AUDIT_PROBE_FAILED "<<e.what()<<'\n';code=1;}
    if(source)PostMessageW(source,WM_CLOSE,0,0);
    if(child.hProcess){WaitForSingleObject(child.hProcess,5000);CloseHandle(child.hProcess);CloseHandle(child.hThread);}
    return code;
}
