// GPL-3.0; owned benign desktop window fixture, no foreign process injection.
#include "../../apps/NeuRoticWindowWorker/WorkerContracts.h"
#include "../../apps/NeuRoticWindowWorker/capture/ComparisonOutput.h"
#include "../../apps/NeuRoticWindowWorker/capture/SnapshotFile.h"
#include <winrt/base.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <dwmapi.h>
#include <fstream>
#include <utility>
namespace nrw { bool TestCaptureCompletionFailures(); }

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<unsigned char> DecodePng(const std::vector<unsigned char>& encoded,uint32_t width,uint32_t height){
 using namespace nrw;ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
 ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),DWORD(encoded.size())));
 ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));UINT w=0,h=0;winrt::check_hresult(frame->GetSize(&w,&h));Check(w==width&&h==height,"PNG preserves output extent");
 ComPtr<IWICFormatConverter> converter;winrt::check_hresult(factory->CreateFormatConverter(&converter));winrt::check_hresult(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
 std::vector<unsigned char> pixels(size_t(width)*height*4);winrt::check_hresult(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()));return pixels;
}
void ComparisonAndSnapshotPixels(){
 using namespace nrw;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
 ComparisonOutput compositor;size_t checks=0;
 for(auto extent:std::vector<std::pair<unsigned,unsigned>>{{13,3},{2,7},{64,16}}){
 const auto [width,height]=extent;D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> original,enhanced,output,readback;
 std::vector<uint32_t> a(size_t(width)*height),b(a.size());for(size_t i=0;i<a.size();++i){a[i]=uint32_t(i*0x01030507u);b[i]=~a[i];}D3D11_SUBRESOURCE_DATA data{a.data(),width*4,0};
 winrt::check_hresult(device->CreateTexture2D(&desc,&data,&original));data.pSysMem=b.data();winrt::check_hresult(device->CreateTexture2D(&desc,&data,&enhanced));winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,&output));
 desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,&readback));
 for(int direction=0;direction<8;++direction)for(auto mode:std::vector<std::pair<float,int>>{{0,0},{1,0},{.2f,0},{.5f,0},{.8f,0},{0,6},{0,32}}){
  compositor.Compose(context.Get(),output.Get(),original.Get(),enhanced.Get(),width,height,mode.first,mode.second,direction);context->CopyResource(readback.Get(),output.Get());
  D3D11_MAPPED_SUBRESOURCE map{};winrt::check_hresult(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map));
  std::vector<unsigned char> png;WriteSnapshotPng(map.pData,width,height,map.RowPitch,[&](const void* bytes,DWORD count){auto p=static_cast<const unsigned char*>(bytes);png.insert(png.end(),p,p+count);});
  Check(png.size()>8&&memcmp(png.data(),"\x89PNG\r\n\x1a\n",8)==0,"snapshot is directly encoded PNG");auto decoded=DecodePng(png,width,height);
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){bool useOriginal=false;
   if(mode.second){for(int i=0;i<mode.second;i+=2)useOriginal|=x>=width*unsigned(i)/mode.second&&x<width*unsigned(i+1)/mode.second;}
   else if(mode.first>=1)useOriginal=true;
   else if(direction==0)useOriginal=x<unsigned(width*mode.first);
   else if(direction==1)useOriginal=x>=width-unsigned(width*mode.first);
   else if(direction==2)useOriginal=y<unsigned(height*mode.first);
   else if(direction==3)useOriginal=y>=height-unsigned(height*mode.first);
   else {float u=(float(x)+.5f)/width,v=(float(y)+.5f)/height;if(direction==5||direction==7)u=1-u;if(direction==6||direction==7)v=1-v;useOriginal=(u+v)*.5f<mode.first;}
   uint32_t gpu=0,pixel=0;memcpy(&gpu,static_cast<const unsigned char*>(map.pData)+size_t(y)*map.RowPitch+x*4,4);memcpy(&pixel,decoded.data()+(size_t(y)*width+x)*4,4);const auto expected=useOriginal?a[size_t(y)*width+x]:b[size_t(y)*width+x];
   Check(gpu==expected&&pixel==expected,"GPU direction and decoded PNG preserve exact source BGRA including alpha");++checks;
  }context->Unmap(readback.Get(),0);compositor.ReleaseFrameViews();
 }
 bool refused=false;try{WriteSnapshotPng(a.data(),width,height,width*4-1,[](const void*,DWORD){});}catch(...){refused=true;}Check(refused,"undersized row pitch refuses before snapshot write");
 }
 std::cout<<"PASS "<<checks<<" exact BGRA pixel checks across eight directions, PNG decode, endpoints, unequal extents and stripe counts\n";
}
void SnapshotPublication(){
 using namespace nrw;
 const auto folder=std::filesystem::current_path()/L"builds/window-worker/capture/snapshot-test";
 std::filesystem::create_directories(folder);Check(std::filesystem::is_empty(folder),"publication fixture starts empty without deleting unknown files");
 const uint32_t pixels[]={0x00112233,0x44556677,0x8899aabb,0xccddeeff};
 const auto name=PublishSnapshot(folder,[&](auto write){WriteSnapshotPng(pixels,2,2,8,write);});
 Check(std::filesystem::path(name).extension()==L".png","published snapshot uses PNG extension");
 std::ifstream input(name,std::ios::binary);std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());input.close();
 const auto decoded=DecodePng(encoded,2,2);Check(decoded.size()==sizeof(pixels)&&memcmp(decoded.data(),pixels,sizeof(pixels))==0,"published PNG decodes to exact input bytes");
 Check(std::distance(std::filesystem::directory_iterator(folder),std::filesystem::directory_iterator{})==1,"successful publication leaves only PNG, no BMP or pending source");
 std::filesystem::remove(name);
 bool failed=false;try{PublishSnapshot(folder,[&](auto write){const BYTE partial=137;write(&partial,1);throw std::runtime_error("injected encoding failure after partial write");});}catch(const std::exception&){failed=true;}
 Check(failed&&std::filesystem::is_empty(folder),"encoding failure removes its exclusively created pending output");
 std::cout<<"PASS atomic PNG publication and partial-write failure cleanup; no temporary/BMP leftovers\n";
}
bool FocusFixture(HWND hwnd) {
    SetWindowPos(hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    RECT client{};GetClientRect(hwnd,&client);POINT center{client.right/2,client.bottom/2},old{};
    ClientToScreen(hwnd,&center);GetCursorPos(&old);SetCursorPos(center.x,center.y);
    INPUT clicks[2]{};for(auto& click:clicks)click.type=INPUT_MOUSE;
    clicks[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;clicks[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    if(SendInput(2,clicks,sizeof(INPUT))!=2)return false;
    const auto end=GetTickCount64()+1500;
    while(GetForegroundWindow()!=hwnd&&GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}
    SetCursorPos(old.x,old.y);return GetForegroundWindow()==hwnd;
}
LRESULT CALLBACK SourceProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT rc{}; GetClientRect(hwnd, &rc);
        RECT left = rc; left.right = rc.right / 2;
        HBRUSH red = CreateSolidBrush(RGB(220, 30, 20)), green = CreateSolidBrush(RGB(20, 210, 40));
        FillRect(dc, &rc, green); FillRect(dc, &left, red); DeleteObject(red); DeleteObject(green);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_APP + 1) { SetWindowPos(hwnd, nullptr, 40, 40, 500, 310, SWP_NOZORDER); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
    if (msg == WM_APP + 2) { SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW); return 0; }
    if (msg == WM_APP + 3) { SetWindowLongPtrW(hwnd, GWL_EXSTYLE, 0); return 0; }
    if (msg == WM_APP + 4) { ShowWindow(hwnd, SW_HIDE); return 0; }
    if (msg == WM_APP + 5) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); return 0; }
    if (msg == WM_APP + 6) { SetWindowPos(hwnd,nullptr,320,160,0,0,SWP_NOSIZE|SWP_NOZORDER);return 0; }
    if (msg == WM_APP + 7) { SetWindowPos(hwnd,nullptr,0,0,560,360,SWP_NOMOVE|SWP_NOZORDER);return 0; }
    if (msg == WM_APP + 10) {auto other=CreateWindowExW(0,L"STATIC",L"Other application focus fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,980,80,260,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);SetPropW(hwnd,L"NRWForeignWindow",other);return 0;}
    if (msg == WM_APP + 11) {auto other=reinterpret_cast<HWND>(RemovePropW(hwnd,L"NRWForeignWindow"));if(other)DestroyWindow(other);return 0;}
    if (msg == WM_APP + 9) {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Fixture menu item");POINT pt{};GetCursorPos(&pt);
        SetPropW(hwnd,L"NRWMenuOpen",reinterpret_cast<HANDLE>(1));TrackPopupMenu(menu,TPM_RETURNCMD,pt.x,pt.y,0,hwnd,nullptr);
        RemovePropW(hwnd,L"NRWMenuOpen");DestroyMenu(menu);return 0;
    }
    if (msg == WM_TIMER) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
    if (msg == WM_LBUTTONDOWN) { SetPropW(hwnd, L"NRWClicked", reinterpret_cast<HANDLE>(1)); return 0; }
    if (msg == WM_MOUSEWHEEL) { SetPropW(hwnd, L"NRWWheel", reinterpret_cast<HANDLE>(1)); return 0; }
    if (msg == WM_KEYDOWN && wp=='K') {SetPropW(hwnd,L"NRWKey",reinterpret_cast<HANDLE>(1));return 0;}
    if (msg == WM_INPUT) {
        RAWINPUT input{};UINT size=sizeof(input);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=UINT(-1) && input.header.dwType==RIM_TYPEKEYBOARD && input.data.keyboard.VKey=='K' && !(input.data.keyboard.Flags&RI_KEY_BREAK))SetPropW(hwnd,L"NRWRawKey",reinterpret_cast<HANDLE>(1));
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int Fixture(const wchar_t* title) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{}; cls.lpfnWndProc = SourceProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"NRWOwnedFixture";cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    RegisterClassW(&cls);
    HWND hwnd = CreateWindowExW(0, cls.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 380, 260, nullptr, nullptr, cls.hInstance, nullptr);
    if (!hwnd) return 2;
    RAWINPUTDEVICE keyboard{1,6,0,hwnd};if(!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard)))return 3;
    SetTimer(hwnd, 1, 60, nullptr);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
nrw::CapturedFrame WaitFrame(nrw::CaptureHost& host, uint32_t oldWidth = 0) {
    ULONGLONG end = GetTickCount64() + 6000;
    while (GetTickCount64() < end) {
        Check(host.Pump(), "capture Pump stopped unexpectedly");
        nrw::CapturedFrame frame; std::string reason;
        if (host.Next(frame, reason) && frame.stamp.width != oldWidth) return frame;
        if (!reason.empty()) throw std::runtime_error(reason);
        Sleep(10);
    }
    throw std::runtime_error("actual WGC frame timed out");
}
void Pixels(nrw::CaptureHost& host, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    Check(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM, "capture must be BGRA8");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    nrw::ComPtr<ID3D11Texture2D> readback;
    Check(SUCCEEDED(host.Device()->CreateTexture2D(&desc, nullptr, &readback)), "staging texture");
    host.Context()->CopyResource(readback.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(host.Context()->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "completed pixel readback");
    auto pixel = [&](UINT x) { return static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * (desc.Height / 2) + x * 4; };
    const auto* l = pixel(desc.Width / 4); const auto* r = pixel(desc.Width * 3 / 4);
    bool correct = l[2] > 160 && l[1] < 80 && r[1] > 160 && r[2] < 80;
    host.Context()->Unmap(readback.Get(), 0);
    Check(correct, "owned red/green source pixels must survive capture");
}
}
// Each explicit capture session selects an actual hardware D11 binding with
// its own generation. This capture-only fixture does not qualify D12 inference.
namespace {
bool StartCaptureSession(nrw::CaptureHost& host,nrw::CaptureOptions& options,std::string& reason){
    host.Stop();
    if(host.Device()){reason="Owned capture fixture could not retire the previous device";return false;}
    nrw::adapters::Binding binding;nrw::adapters::Report report;
    if(!nrw::adapters::Select(options.session,options.target.width,options.target.height,false,binding,report,reason))return false;
    options.processing=std::move(binding);
    const bool started=host.Start(options, reason);
    options.processing={}; // the actual capture owner retains an adopted binding
    return started;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--fixture") return Fixture(argv[2]);
    PROCESS_INFORMATION process{}; HWND source = nullptr;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Check(nrw::TestCaptureCompletionFailures(), "capture fence faults retain all in-flight owners");
        ComparisonAndSnapshotPixels();
        SnapshotPublication();
        if(argc>1&&std::wstring(argv[1])==L"--completion-only"){std::cout<<"PASS synthetic WARP comparison pixels, lossless PNG decode and capture/snapshot owner retention through six completion faults; no live capture\n";return 0;}
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring title = L"NRW capture fixture " + std::to_wstring(GetCurrentProcessId());
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --fixture \"" + title + L"\"";
        STARTUPINFOW si{sizeof(si)};
        Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &process) != FALSE, "owned source process");
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (!source && GetTickCount64() < deadline) { source = FindWindowW(L"NRWOwnedFixture", title.c_str()); Sleep(20); }
        Check(source != nullptr, "owned source window");
        nrw::WindowIdentity id; std::string reason;
        Check(nrw::CaptureHost::Inspect(source, id, reason), "Inspect accepts owned external top-level source");
        Check(id.pid == process.dwProcessId && id.processCreation != 0, "stable process creation identity");
        Check(nrw::CaptureHost::Validate(id, reason), "fresh identity validates");
        auto wrong = id; wrong.processCreation++; Check(!nrw::CaptureHost::Validate(wrong, reason), "PID/creation mismatch refuses reused identity");
        wrong = id; wrong.pid++; Check(!nrw::CaptureHost::Validate(wrong, reason), "PID mismatch refuses reused HWND");
        wrong = id; wrong.processCreation = 0; Check(!nrw::CaptureHost::Validate(wrong, reason), "missing process creation refuses");
        auto windows = nrw::CaptureHost::Enumerate(); Check(std::any_of(windows.begin(), windows.end(), [&](const auto& w){ return w.hwnd == id.hwnd; }), "enumeration contains fixture");
        ShowWindow(source, SW_MINIMIZE); Sleep(100); Check(!nrw::CaptureHost::Validate(id, reason), "minimized source refuses"); ShowWindow(source, SW_RESTORE); Sleep(100);
        SendMessageW(source, WM_APP + 2, 0, 0); Check(!nrw::CaptureHost::Validate(id, reason), "tool window refuses"); SendMessageW(source, WM_APP + 3, 0, 0);
        SendMessageW(source, WM_APP + 4, 0, 0); Check(!nrw::CaptureHost::Validate(id, reason), "hidden source refuses"); SendMessageW(source, WM_APP + 5, 0, 0);
        if (argc > 1 && std::wstring(argv[1]) == L"--identity") {
            PostMessageW(source, WM_CLOSE, 0, 0); Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "identity fixture closes"); source = nullptr;
            Check(!nrw::CaptureHost::Validate(id, reason), "destroyed source refuses");
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            std::cout << "PASS native identity: owned catalog, HWND/PID/creation/executable/class, mismatch/missing creation, minimized, tool, hidden, destroyed filters\n"; return 0;
        }
        nrw::CaptureHost host; nrw::CaptureOptions options{id, 42, true};
        Check(FocusFixture(source),"source is foreground before overlay activation through actual input");
        RECT cursorClient{};GetClientRect(source,&cursorClient);POINT liveCursor{cursorClient.right/4,cursorClient.bottom/2};ClientToScreen(source,&liveCursor);SetCursorPos(liveCursor.x,liveCursor.y);Sleep(100);
        CURSORINFO sourceCursor{sizeof(sourceCursor)};Check(GetCursorInfo(&sourceCursor)&&(sourceCursor.flags&CURSOR_SHOWING),"source system cursor is visible");
        if (!StartCaptureSession(host,options,reason)) throw std::runtime_error("Start: " + reason);
        Check(host.WakeHandle()!=nullptr && WaitForSingleObject(host.WakeHandle(),3000)==WAIT_OBJECT_0,"real WGC arrival signals owner wake event");
        Check(!host.CursorCaptured(),"overlay excludes cursor from delayed model frames despite requested capture");
        HWND startingOutput=FindWindowW(L"NeuRoticWindowWorkerOutput",nullptr);
        Check(startingOutput&&!IsWindowVisible(startingOutput),"overlay stays hidden until the first accepted frame");
        auto frame = WaitFrame(host); Check(frame.stamp.session == 42 && frame.stamp.reset && frame.stamp.sequence == 1, "first capture generation resets"); Pixels(host, frame.texture.Get());
        RECT client{}; GetClientRect(source, &client);
        std::cout << "fixture client=" << client.right << 'x' << client.bottom << " captured=" << frame.stamp.width << 'x' << frame.stamp.height << '\n';
        Check(frame.stamp.width == static_cast<uint32_t>(client.right) && frame.stamp.height == static_cast<uint32_t>(client.bottom), "captured raster is exact client area without chrome");
        nrw::ComPtr<ID3D11Texture2D> blue; D3D11_TEXTURE2D_DESC desc{}; frame.texture->GetDesc(&desc);
        Check(SUCCEEDED(host.Device()->CreateTexture2D(&desc, nullptr, &blue)), "processed texture");
        nrw::ComPtr<ID3D11RenderTargetView> rtv; Check(SUCCEEDED(host.Device()->CreateRenderTargetView(blue.Get(), nullptr, &rtv)), "processed RTV");
        float colour[]{0, 0, 1, 1}; host.Context()->ClearRenderTargetView(rtv.Get(), colour);
        for (float split : {1.f, 0.f, 0.5f, -1.f}) Check(host.Present(frame, blue.Get(), split, reason), "original/processed/wipe present");
        HWND output = FindWindowW(L"NeuRoticWindowWorkerOutput", nullptr); Check(output != nullptr, "owned output window");
        Check(!(GetWindowLongPtrW(output,GWL_STYLE)&WS_CAPTION), "overlay must have no title bar");
        RECT sourceScreen=client,outputScreen{};MapWindowPoints(source,nullptr,reinterpret_cast<POINT*>(&sourceScreen),2);GetWindowRect(output,&outputScreen);
        Check(EqualRect(&sourceScreen,&outputScreen), "overlay follows exact source client geometry");
        const POINT center{sourceScreen.left+client.right/2,sourceScreen.top+client.bottom/2};
        SetForegroundWindow(source);SetWindowPos(source,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);Sleep(100);host.Pump();
        Check(host.Present(frame,blue.Get(),0,reason),"processed overlay presents for pixel test");
        Check(SetWindowDisplayAffinity(output,WDA_NONE)!=FALSE,"owned pixel probe temporarily allows capture");DwmFlush();Sleep(100);
        HDC screen=GetDC(nullptr);const auto displayed=GetPixel(screen,center.x,center.y);ReleaseDC(nullptr,screen);
        Check(SetWindowDisplayAffinity(output,WDA_EXCLUDEFROMCAPTURE)!=FALSE,"restore overlay capture exclusion");
        Check(displayed!=CLR_INVALID&&GetBValue(displayed)>200&&GetRValue(displayed)<20&&GetGValue(displayed)<20,"actual overlay displays processed blue pixels");
        SendMessageW(source,WM_LBUTTONDOWN,0,MAKELPARAM(client.right/2,client.bottom/2));
        Check(GetPropW(source,L"NRWClicked")!=nullptr,"source fixture input handler accessible");RemovePropW(source,L"NRWClicked");
        POINT oldCursor{},actualCursor{};GetCursorPos(&oldCursor);Check(SetCursorPos(center.x,center.y)!=FALSE,"position test cursor");GetCursorPos(&actualCursor);
        std::cout<<"cursor="<<actualCursor.x<<','<<actualCursor.y<<" center="<<center.x<<','<<center.y<<'\n';
        INPUT input[3]{};for(auto& i:input)i.type=INPUT_MOUSE;input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;input[2].mi.dwFlags=MOUSEEVENTF_WHEEL;input[2].mi.mouseData=WHEEL_DELTA;
        ShowWindow(output,SW_HIDE);
        auto waitInput=[&]{auto until=GetTickCount64()+1500;while(GetTickCount64()<until){host.Pump();if(GetPropW(source,L"NRWClicked")&&GetPropW(source,L"NRWWheel"))break;Sleep(10);}};
        Check(SendInput(3,input,sizeof(INPUT))==3,"owned physical mouse input baseline");waitInput();
        const bool inputBaseline=GetPropW(source,L"NRWClicked")&&GetPropW(source,L"NRWWheel");
        std::cout<<"uncovered input baseline="<<inputBaseline<<'\n';RemovePropW(source,L"NRWClicked");RemovePropW(source,L"NRWWheel");ShowWindow(output,SW_SHOWNOACTIVATE);Sleep(100);
        Check(GetAncestor(WindowFromPoint(center),GA_ROOT)==source,"overlay cross-process hit testing reaches source");
        Check(SendInput(3,input,sizeof(INPUT))==3,"owned physical mouse input");waitInput();SetCursorPos(oldCursor.x,oldCursor.y);
        std::cout<<"mouse clicked="<<bool(GetPropW(source,L"NRWClicked"))<<" wheel="<<bool(GetPropW(source,L"NRWWheel"))<<" foreground="<<GetForegroundWindow()<<" source="<<source<<" output="<<output<<" hit="<<WindowFromPoint(center)<<" style="<<GetWindowLongPtrW(output,GWL_EXSTYLE)<<'\n';
        if(inputBaseline)Check(GetPropW(source,L"NRWClicked")&&GetPropW(source,L"NRWWheel"),"actual click and wheel pass through to external source");
        else std::cout<<"LIMIT physical mouse delivery unavailable even without overlay; native cross-process hit test passed\n";
        INPUT keys[2]{};for(auto& key:keys){key.type=INPUT_KEYBOARD;key.ki.wVk='K';}keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
        Check(SendInput(2,keys,sizeof(INPUT))==2,"physical keyboard fixture input");
        auto keyDeadline=GetTickCount64()+1500;while(GetTickCount64()<keyDeadline&&(!GetPropW(source,L"NRWKey")||!GetPropW(source,L"NRWRawKey"))){host.Pump();Sleep(10);}
        Check(GetPropW(source,L"NRWKey")&&GetPropW(source,L"NRWRawKey"),"native keyboard and Raw Input remain with source");
        nrw::WindowIdentity rejected; Check(!nrw::CaptureHost::Inspect(output, rejected, reason), "own output excluded");
        Check(!host.Present(frame, nullptr, 0, reason) && !reason.empty(), "missing processed texture refuses processed view");
        auto wrongSession = frame; wrongSession.stamp.session++; Check(!host.Present(wrongSession, blue.Get(), 0, reason), "foreign session refuses");
        auto first = frame; auto next = WaitFrame(host); Check(next.stamp.sequence > frame.stamp.sequence && next.stamp.timestampQpc>frame.stamp.timestampQpc && next.texture.Get() != frame.texture.Get(), "bounded polling creates independent immutable snapshots");
        Check(next.stamp.reset,"long input-test delivery gap resets temporal history");Pixels(host, first.texture.Get());
        Check(!host.Present(first, nullptr, 1, reason), "older original sequence refuses presentation");
        auto wrongStamp = next; wrongStamp.stamp.timestampQpc++; Check(!host.Present(wrongStamp, nullptr, 1, reason), "forged timestamp refuses presentation");
        PostMessageW(source, WM_APP + 1, 0, 0); auto resized = WaitFrame(host, frame.stamp.width);
        GetClientRect(source, &client); Check(resized.stamp.width == static_cast<uint32_t>(client.right) && resized.stamp.height == static_cast<uint32_t>(client.bottom), "resized raster is exact client extent");
        Check(resized.stamp.reset && resized.stamp.sequence > next.stamp.sequence, "extent discontinuity resets"); Pixels(host, resized.texture.Get()); Check(host.Present(resized, nullptr, 1, reason), "resized original presents");
        Check(!host.Present(first, nullptr, 1, reason), "stale extent refuses presentation");
        SendMessageW(source,WM_APP+6,0,0);Check(host.Present(resized,nullptr,1,reason),"in-flight source move still presents matching raster");
        GetClientRect(source,&client);sourceScreen=client;MapWindowPoints(source,nullptr,reinterpret_cast<POINT*>(&sourceScreen),2);GetWindowRect(output,&outputScreen);
        Check(EqualRect(&sourceScreen,&outputScreen),"in-flight move follows source before publication");
        SendMessageW(source,WM_APP+7,0,0);Check(!host.Present(resized,nullptr,1,reason)&&reason.empty(),"in-flight resize drops old raster without failing session");
        Check(!IsWindowVisible(output),"stale extent is hidden until matching output arrives");
        auto aligned=WaitFrame(host,resized.stamp.width);Check(aligned.stamp.reset&&host.Present(aligned,nullptr,1,reason),"matching resized output resets and resumes");
        ShowWindow(source,SW_MINIMIZE);Sleep(100);Check(host.Pump(),"minimized source pauses instead of ending capture");
        Check(!IsWindowVisible(output),"minimized source hides overlay");
        ShowWindow(source,SW_RESTORE);Sleep(100);Check(host.Pump(),"restored source resumes capture");
        Check(!IsWindowVisible(output),"restore keeps old output hidden until fresh presentation");
        auto resumed=WaitFrame(host);Check(resumed.stamp.reset,"restore resets temporal history");
        Check(host.Present(resumed,nullptr,1,reason)&&IsWindowVisible(output),"fresh restored output becomes visible");
        WNDCLASSW otherClass{};otherClass.lpfnWndProc=DefWindowProcW;otherClass.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));otherClass.hInstance=GetModuleHandleW(nullptr);otherClass.lpszClassName=L"NRWOtherInteractionFixture";RegisterClassW(&otherClass);
        HWND other=CreateWindowExW(0,otherClass.lpszClassName,L"Other application fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,720,80,260,180,nullptr,nullptr,otherClass.hInstance,nullptr);
        Check(other&&FocusFixture(other),"controlled foreground leaves source");
        Check(host.Pump()&&!host.Paused()&&IsWindowVisible(output),"Alt-Tab keeps valid capture and overlay running");
        auto background=WaitFrame(host);Check(background.stamp.sequence>resumed.stamp.sequence&&host.Present(background,nullptr,1,reason),"background capture continues publishing newer frames");
        Check(GetForegroundWindow()==other,"processing never steals unrelated foreground focus");
        Check(FocusFixture(source),"source returns to foreground");DestroyWindow(other);
        Check(host.Pump()&&!host.Paused()&&IsWindowVisible(output),"focus return keeps existing session running");
        auto refocused=WaitFrame(host);Check(host.Present(refocused,nullptr,1,reason),"focus return continues output");
        HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME,otherClass.lpszClassName,L"Source dialog fixture",WS_POPUP|WS_CAPTION|WS_VISIBLE,460,200,260,180,source,nullptr,otherClass.hInstance,nullptr);
        EnableWindow(source,FALSE);Check(dialog&&FocusFixture(dialog),"source owned dialog takes foreground");
        Check(host.Pump()&&host.Paused()&&!IsWindowVisible(output),"owned dialog remains unobscured");
        Check(!host.Present(refocused,nullptr,1,reason)&&reason.empty()&&GetForegroundWindow()==dialog,"late output cannot cover or reactivate a modal dialog");
        EnableWindow(source,TRUE);DestroyWindow(dialog);Check(FocusFixture(source),"return from source dialog");auto afterDialog=WaitFrame(host);Check(afterDialog.stamp.reset&&host.Present(afterDialog,nullptr,1,reason),"dialog close resumes fresh output");
        PostMessageW(source,WM_APP+9,0,0);auto menuDeadline=GetTickCount64()+1500;GUITHREADINFO menuInfo{sizeof(menuInfo)};
        while(GetTickCount64()<menuDeadline){GetGUIThreadInfo(GetWindowThreadProcessId(source,nullptr),&menuInfo);if(menuInfo.flags&GUI_INMENUMODE)break;Sleep(10);}
        Check(menuInfo.flags&GUI_INMENUMODE,"real native popup menu opens");Check(host.Pump()&&host.Paused()&&!IsWindowVisible(output),"native menu yields processing overlay");
        PostMessageW(source,WM_CANCELMODE,0,0);menuDeadline=GetTickCount64()+1500;while(GetPropW(source,L"NRWMenuOpen")&&GetTickCount64()<menuDeadline)Sleep(10);
        Check(!GetPropW(source,L"NRWMenuOpen"),"native popup menu dismisses");Check(FocusFixture(source),"source active after menu");auto afterMenu=WaitFrame(host);Check(afterMenu.stamp.reset&&host.Present(afterMenu,nullptr,1,reason),"menu dismissal resumes fresh output");
        Check(host.DisengageHotkeyAvailable(),"active overlay owns disengage shortcut");
        const auto priorFocus=GetForegroundWindow();host.Disengage();Check(!IsWindowVisible(output)&&GetForegroundWindow()==priorFocus&&host.Device()!=nullptr,"disengage hides output without stealing focus or retiring GPU resources");
        Check(!GetWindow(output,GW_OWNER)&&!host.DisengageHotkeyAvailable(),"disengage detaches owner and releases shortcut before retirement");
        Check(RegisterHotKey(nullptr,0x1337,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F10)!=FALSE,"shortcut is reusable immediately after disengage");UnregisterHotKey(nullptr,0x1337);
        Check(!host.Present(afterMenu,nullptr,1,reason),"disengaged session cannot publish late output");host.Stop();
        options.session=48;options.controlHostPid=GetCurrentProcessId();Check(StartCaptureSession(host,options,reason),"control-host comparison session starts");
        auto comparisonFrame=WaitFrame(host);nrw::ComPtr<ID3D11Texture2D> comparisonBlue;comparisonFrame.texture->GetDesc(&desc);
        Check(SUCCEEDED(host.Device()->CreateTexture2D(&desc,nullptr,&comparisonBlue)),"comparison processed texture");rtv.Reset();
        Check(SUCCEEDED(host.Device()->CreateRenderTargetView(comparisonBlue.Get(),nullptr,&rtv)),"comparison processed RTV");host.Context()->ClearRenderTargetView(rtv.Get(),colour);
        Check(host.Present(comparisonFrame,comparisonBlue.Get(),0,reason),"comparison source first processed presentation");output=FindWindowW(L"NeuRoticWindowWorkerOutput",nullptr);
        HWND control=CreateWindowExW(0,otherClass.lpszClassName,L"NeuRotic control-host fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,720,80,260,180,nullptr,nullptr,otherClass.hInstance,nullptr);
        Check(control&&FocusFixture(control),"comparison control host takes focus through actual input");
        Check(host.Pump()&&!host.Paused()&&IsWindowVisible(output),"comparison control host keeps NR overlay alive");
        auto controlFrame=WaitFrame(host);Check(controlFrame.stamp.session==comparisonFrame.stamp.session&&controlFrame.stamp.sequence>comparisonFrame.stamp.sequence,"using App controls continues the same capture session");
        for(float wipe:{0.f,.44f,1.f,0.f})Check(host.Present(controlFrame,comparisonBlue.Get(),wipe,reason)&&IsWindowVisible(output)&&GetForegroundWindow()==control,"live comparison changes preserve overlay and App focus");
        GetClientRect(source,&client);sourceScreen=client;MapWindowPoints(source,nullptr,reinterpret_cast<POINT*>(&sourceScreen),2);
        POINT processedProbe{sourceScreen.left+client.right*3/4,sourceScreen.top+client.bottom/2};
        Check(host.Present(controlFrame,comparisonBlue.Get(),.44f,reason),"comparison wipe presents while App is foreground");
        Check(SetWindowDisplayAffinity(output,WDA_NONE)!=FALSE,"comparison pixel probe allows capture");DwmFlush();Sleep(100);
        screen=GetDC(nullptr);auto originalLeft=GetPixel(screen,sourceScreen.left+client.right/4,sourceScreen.top+client.bottom/2);auto processedRight=GetPixel(screen,processedProbe.x,processedProbe.y);ReleaseDC(nullptr,screen);
        Check(SetWindowDisplayAffinity(output,WDA_EXCLUDEFROMCAPTURE)!=FALSE,"comparison pixel probe restores exclusion");
        std::cout<<"comparison pixels left="<<originalLeft<<" right="<<processedRight<<" source="<<sourceScreen.left<<','<<sourceScreen.top<<'-'<<sourceScreen.right<<','<<sourceScreen.bottom<<" control foreground="<<GetForegroundWindow()<<" source above="<<GetWindow(source,GW_HWNDPREV)<<" output="<<output<<'\n';
        Check(originalLeft==RGB(220,30,20)&&processedRight!=CLR_INVALID&&GetBValue(processedRight)>200&&GetRValue(processedRight)<20,"App-focused 44 percent comparison displays original red and processed blue");
        SetWindowPos(control,HWND_TOP,sourceScreen.left+20,sourceScreen.top+20,220,180,SWP_NOACTIVATE);DwmFlush();Sleep(50);
        Check(host.Pump()&&host.Present(controlFrame,comparisonBlue.Get(),.44f,reason),"comparison output continues behind overlapping App controls");
        POINT controlPoint{40,40};ClientToScreen(control,&controlPoint);Check(WindowFromPoint(controlPoint)==control&&GetForegroundWindow()==control,"overlay never covers or activates over its App controls");
        Check(SetWindowDisplayAffinity(output,WDA_NONE)!=FALSE,"overlapping App pixel probe allows capture");DwmFlush();Sleep(100);screen=GetDC(nullptr);auto appPixel=GetPixel(screen,controlPoint.x,controlPoint.y);ReleaseDC(nullptr,screen);Check(SetWindowDisplayAffinity(output,WDA_EXCLUDEFROMCAPTURE)!=FALSE,"overlapping App pixel probe restores exclusion");
        Check(appPixel!=CLR_INVALID&&GetRValue(appPixel)>200&&GetGValue(appPixel)>200&&GetBValue(appPixel)>200,"App remains visually above the processed overlay");
        EnableWindow(source,FALSE);Check(host.Pump()&&host.Paused()&&!IsWindowVisible(output),"disabled source still yields while App controls are foreground");EnableWindow(source,TRUE);
        Check(FocusFixture(source),"source returns after comparison control interaction");DestroyWindow(control);auto afterControls=WaitFrame(host);Check(afterControls.stamp.reset&&host.Present(afterControls,nullptr,1,reason),"disabled-source recovery remains fresh after App comparison");
        SendMessageW(source,WM_APP+10,0,0);auto foreign=reinterpret_cast<HWND>(GetPropW(source,L"NRWForeignWindow"));Check(foreign&&FocusFixture(foreign),"non-control foreground window takes actual input focus");
        Check(host.Pump()&&!host.Paused()&&IsWindowVisible(output),"App blur to another process keeps rendering active");auto foreignFrame=WaitFrame(host);Check(foreignFrame.stamp.sequence>afterControls.stamp.sequence&&host.Present(foreignFrame,nullptr,1,reason)&&GetForegroundWindow()==foreign,"background output advances without taking foreground focus");
        SetWindowPos(foreign,HWND_TOP,sourceScreen.left+20,sourceScreen.top+20,220,180,SWP_NOACTIVATE);POINT foreignPoint{40,40};ClientToScreen(foreign,&foreignPoint);Check(host.Pump()&&host.Present(foreignFrame,nullptr,1,reason)&&WindowFromPoint(foreignPoint)==foreign,"overlapping foreign app stays above background overlay and keeps input");
        Check(FocusFixture(source),"source returns from non-control foreground");SendMessageW(source,WM_APP+11,0,0);auto afterForeign=WaitFrame(host);Check(host.Present(afterForeign,nullptr,1,reason),"other application return continues output");host.Stop();options.controlHostPid=0;
        options.session=45;Check(StartCaptureSession(host,options,reason),"restart after explicit disengage");auto hotkeyFrame=WaitFrame(host);Check(host.Present(hotkeyFrame,nullptr,1,reason),"hotkey session presents");output=FindWindowW(L"NeuRoticWindowWorkerOutput",nullptr);
        INPUT shortcut[6]{};WORD shortcutKeys[]={VK_CONTROL,VK_MENU,VK_F10,VK_F10,VK_MENU,VK_CONTROL};for(int k=0;k<6;++k){shortcut[k].type=INPUT_KEYBOARD;shortcut[k].ki.wVk=shortcutKeys[k];if(k>=3)shortcut[k].ki.dwFlags=KEYEVENTF_KEYUP;}
        Check(SendInput(6,shortcut,sizeof(INPUT))==6,"actual disengage shortcut input");auto shortcutDeadline=GetTickCount64()+1500;bool alive=true;while(alive&&GetTickCount64()<shortcutDeadline){alive=host.Pump();Sleep(10);}
        Check(!alive&&!IsWindowVisible(output)&&GetForegroundWindow()==source,"actual Ctrl+Alt+F10 disengages without focusing output");host.Stop();
        Check(RegisterHotKey(nullptr,0x1337,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F10)!=FALSE,"fixture owns shortcut conflict");options.session=46;Check(StartCaptureSession(host,options,reason)&&!host.DisengageHotkeyAvailable(),"shortcut conflict does not prevent capture and is reported");UnregisterHotKey(nullptr,0x1337);WaitFrame(host);output=FindWindowW(L"NeuRoticWindowWorkerOutput",nullptr);
        PostMessageW(output, WM_KEYDOWN, VK_ESCAPE, 0); Check(!host.Pump(), "Escape stops output"); host.Stop(); host.Stop();
        options.session = 43;options.overlay=false; Check(StartCaptureSession(host,options,reason), "restart after stop");Check(host.CursorCaptured(),"separate preview retains explicit cursor capture"); auto restarted = WaitFrame(host); Check(restarted.stamp.session == 43 && restarted.stamp.reset, "session reset");
        output = FindWindowW(L"NeuRoticWindowWorkerOutput", nullptr); PostMessageW(output, WM_CLOSE, 0, 0); Check(!host.Pump(), "output close stops"); host.Stop();
        options.session = 44; Check(StartCaptureSession(host,options,reason), "second restart"); WaitFrame(host);
        PostMessageW(source, WM_CLOSE, 0, 0); Check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "source closes"); source = nullptr;
        Check(!host.Pump(), "source destruction stops capture"); Check(!nrw::CaptureHost::Validate(id, reason), "destroyed HWND identity invalid"); host.Stop();
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        Check(host.Device() == nullptr && host.Context() == nullptr, "normal Stop releases device/context");
        std::cout << "PASS native overlay startup; live cursor exclusion; physical mouse/keyboard/raw input; focus/dialog/menu yield and fresh resume; App-focused comparison pixels and stacking; real shortcut/conflict/release; immediate disengage before GPU retirement; identity/geometry/immutable raster; preview/restart/source close; normal drained release\n"; return 0;
    } catch (const std::exception& e) {
        if (source) PostMessageW(source, WM_CLOSE, 0, 0);
        if (process.hProcess) { WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess); CloseHandle(process.hThread); }
        std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}
