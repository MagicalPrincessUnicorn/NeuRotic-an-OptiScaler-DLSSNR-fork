#pragma once
// Static bootstrap feedback only. It owns no App/model/graphics initialization.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <limits>

namespace nh {
namespace startup_brand {
#include "brand/StartupBrandPixels.inc"
}
class StartupFeedback {
public:
 struct Receipt {
  HWND window=nullptr;
  DWORD threadId=0,error=0;
  bool painted=false,iconLoaded=false,brandLoaded=false,threadJoined=false,gdiReleased=true;
  UINT dpi=96;RECT client{},mark{},wordmark{},status{};
  LONGLONG paintedAt=0,dismissedAt=0;
 };
 StartupFeedback() noexcept=default;
 StartupFeedback(const StartupFeedback&)=delete;
 StartupFeedback& operator=(const StartupFeedback&)=delete;
 ~StartupFeedback(){Close();}

 bool Start(HINSTANCE instance) noexcept {return StartImpl(instance,false);}
#ifdef NEUROTIC_STARTUP_FEEDBACK_TEST
 bool StartWithCreationFailureForTest(HINSTANCE instance) noexcept {return StartImpl(instance,true);}
 static RECT FitForTest(RECT work,UINT dpi) noexcept {return Fit(work,dpi);}
 static bool PaintForTest(HDC dc,RECT client,UINT dpi,const std::wstring& text,Receipt& receipt) noexcept {
  State state;state.instance=GetModuleHandleW(nullptr);Drawing drawing;drawing.Build(state,dpi,ContentScale(client));
  receipt=state.receipt;const bool drawn=DrawContent(dc,client,drawing,text,receipt);receipt.painted=drawn;
  receipt.gdiReleased=drawing.Release()&&state.cleanupOk;return drawn;
 }
#endif
 bool DismissWhenMainVisible(HWND mainWindow) noexcept {
  if(!mainWindow||!IsWindow(mainWindow)||!IsWindowVisible(mainWindow))return false;
  Close();return true;
 }
 void SetLoadingText(std::wstring text) noexcept {
  try{
   {std::lock_guard lock(state_.mutex);state_.text=std::move(text);}
   if(refresh_)SetEvent(refresh_);
  }catch(...){/* Keep the bootstrap fallback if a copied translation cannot be stored. */}
 }
 Receipt Snapshot() const noexcept {std::lock_guard lock(state_.mutex);return state_.receipt;}
 void Close() noexcept {
  if(stop_)SetEvent(stop_);
  // Only the main owner calls Close; no lock is held while its UI thread exits.
  if(thread_.joinable()){
   thread_.join();
   std::lock_guard lock(state_.mutex);state_.receipt.threadJoined=true;
  }
  for(auto* event:{&ready_,&stop_,&refresh_})if(*event){CloseHandle(*event);*event=nullptr;}
 }

private:
 struct State {
  mutable std::mutex mutex;
  Receipt receipt;
  std::wstring text;
  HANDLE ready=nullptr,stop=nullptr,refresh=nullptr;
  HINSTANCE instance=nullptr;
  bool failCreation=false,cleanupOk=true;
 } state_;
 HANDLE ready_=nullptr,stop_=nullptr,refresh_=nullptr;
 std::thread thread_;
 static constexpr wchar_t WindowClass[]=L"NeuRoticStartupFeedbackWindow";
 static LONGLONG Clock() noexcept {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
 struct Picture {
  std::vector<uint32_t> pixels;int width=0,height=0;
  void Release() noexcept {std::vector<uint32_t>().swap(pixels);width=height=0;}
  bool Build(const unsigned char* packed,size_t bytes,int w,int h) noexcept {
   Release();
   try{
    if(w<=0||h<=0)return false;
    pixels.resize(static_cast<size_t>(w)*h);auto* out=pixels.data();size_t remaining=pixels.size();
    const auto* input=packed;const auto* end=input+bytes;
    const auto rgba=[](const unsigned char* p){return uint32_t(p[2])|(uint32_t(p[1])<<8)|(uint32_t(p[0])<<16)|(uint32_t(p[3])<<24);};
    while(input<end&&remaining){
     if(end-input<2){Release();return false;}
     const unsigned tag=input[0]|(unsigned(input[1])<<8);input+=2;const unsigned count=tag&0x7fff;
     if(!count||count>remaining){Release();return false;}
     if(tag&0x8000){if(end-input<4){Release();return false;}const auto pixel=rgba(input);input+=4;std::fill_n(out,count,pixel);out+=count;}
     else{if(static_cast<size_t>(end-input)<static_cast<size_t>(count)*4){Release();return false;}for(unsigned i=0;i<count;++i){*out++=rgba(input);input+=4;}}
     remaining-=count;
    }
    if(remaining||input!=end){Release();return false;}width=w;height=h;return true;
   }catch(...){Release();return false;}
  }
  bool Draw(HDC dc,RECT target) const noexcept {
   if(pixels.empty()||target.right<=target.left||target.bottom<=target.top)return false;
   BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
   info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
   return StretchDIBits(dc,target.left,target.top,target.right-target.left,target.bottom-target.top,
       0,0,width,height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY)>0;
  }
 };
 struct Drawing {
  HICON icon=nullptr;HFONT loading=nullptr;UINT dpi=96;Picture mark,wordmark;
  bool Release() noexcept {
   bool good=true;
   if(icon){good=DestroyIcon(icon)!=FALSE&&good;icon=nullptr;}
   if(loading){good=DeleteObject(loading)!=FALSE&&good;loading=nullptr;}
   mark.Release();wordmark.Release();return good;
  }
  void Build(State& state,UINT value,double scale) noexcept {
   const bool released=Release();dpi=value?value:96;
   const int iconSize=MulDiv(64,static_cast<int>(dpi),96);
   icon=static_cast<HICON>(LoadImageW(state.instance,MAKEINTRESOURCEW(101),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR));
   // The name is supplied artwork; only the short loading caption uses a font.
   loading=CreateFontW(-(std::max)(12,static_cast<int>(std::lround(16*scale))),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
       DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
   const bool markOk=mark.Build(startup_brand::MarkPacked,sizeof(startup_brand::MarkPacked),startup_brand::MarkWidth,startup_brand::MarkHeight);
   const bool wordmarkOk=wordmark.Build(startup_brand::WordmarkPacked,sizeof(startup_brand::WordmarkPacked),startup_brand::WordmarkWidth,startup_brand::WordmarkHeight);
   std::lock_guard lock(state.mutex);state.receipt.iconLoaded=icon!=nullptr;state.receipt.brandLoaded=markOk&&wordmarkOk;
   state.cleanupOk=state.cleanupOk&&released;state.receipt.gdiReleased=state.cleanupOk&&!(icon||loading);
  }
 };
 struct Context {State* state;Drawing drawing;};
 static RECT WorkArea() noexcept {
  POINT pointer{};GetCursorPos(&pointer);MONITORINFO monitor{sizeof(monitor)};
  if(GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTONEAREST),&monitor))return monitor.rcWork;
  return {0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
 }
 static RECT Fit(RECT work,UINT dpi) noexcept {
  const auto width=(std::max)(1LL,static_cast<long long>(work.right)-work.left);
  const auto height=(std::max)(1LL,static_cast<long long>(work.bottom)-work.top);
  // CreateWindow takes signed pixel dimensions. Fit the whole portrait uniformly.
  const auto availableWidth=(std::min)((std::max)(1LL,width-16),static_cast<long long>((std::numeric_limits<int>::max)()));
  const auto availableHeight=(std::min)((std::max)(1LL,height-16),static_cast<long long>((std::numeric_limits<int>::max)()));
  const double scale=(std::min)({double(dpi?dpi:96)/96,double(availableWidth)/320,double(availableHeight)/440});
  const auto w=(std::max)(1LL,(std::min)(availableWidth,static_cast<long long>(std::lround(320*scale))));
  const auto h=(std::max)(1LL,(std::min)(availableHeight,static_cast<long long>(std::lround(440*scale))));
  const LONG left=static_cast<LONG>(work.left+(width-w)/2),top=static_cast<LONG>(work.top+(height-h)/2);
  return {left,top,static_cast<LONG>(left+w),static_cast<LONG>(top+h)};
 }
 static double ContentScale(RECT client) noexcept {
  return (std::max)(0.0,(std::min)(double(client.right-client.left)/320,double(client.bottom-client.top)/440));
 }
 static RECT ArtworkRect(RECT room,int width,int height) noexcept {
  const double scale=(std::min)(double(room.right-room.left)/width,double(room.bottom-room.top)/height);
  const LONG w=static_cast<LONG>(std::lround(width*scale)),h=static_cast<LONG>(std::lround(height*scale));
  const LONG x=room.left+(room.right-room.left-w)/2,y=room.top+(room.bottom-room.top-h)/2;
  return {x,y,x+w,y+h};
 }
 static void Failure(State& state,DWORD error) noexcept {
  {std::lock_guard lock(state.mutex);state.receipt.error=error?error:ERROR_GEN_FAILURE;}
  SetEvent(state.ready);
 }
 static bool DrawContent(HDC dc,RECT client,const Drawing& drawing,const std::wstring& text,Receipt& layout) noexcept {
  const int saved=SaveDC(dc);if(!saved)return false;
  struct Restore {HDC dc;int saved;~Restore(){RestoreDC(dc,saved);}} restore{dc,saved};
  SetDCBrushColor(dc,RGB(0,0,0));const bool filled=FillRect(dc,&client,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)))!=0;
  const double scale=ContentScale(client);const auto px=[&](int value){return static_cast<LONG>(std::lround(value*scale));};
  const LONG x=client.left+(client.right-client.left-px(320))/2,y=client.top+(client.bottom-client.top-px(440))/2;
  layout.client=client;layout.dpi=drawing.dpi;
  layout.mark=ArtworkRect({x+px(24),y+px(28),x+px(296),y+px(286)},startup_brand::MarkWidth,startup_brand::MarkHeight);
  layout.wordmark=ArtworkRect({x+px(24),y+px(310),x+px(296),y+px(352)},startup_brand::WordmarkWidth,startup_brand::WordmarkHeight);
  layout.status={x+px(24),y+px(374),x+px(296),y+px(418)};
  SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
  const bool markDrawn=drawing.mark.Draw(dc,layout.mark),wordmarkDrawn=drawing.wordmark.Draw(dc,layout.wordmark);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(238,242,248));
  SelectObject(dc,drawing.loading?drawing.loading:reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
  const bool labelled=DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&layout.status,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX)!=0;
  return filled&&markDrawn&&wordmarkDrawn&&labelled;
 }
 static bool Paint(HWND window,Context& context) {
  auto& state=*context.state;std::wstring text;
  {std::lock_guard lock(state.mutex);text=state.text;}
  PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);if(!dc)return false;
  struct End {HWND window;PAINTSTRUCT* paint;~End(){EndPaint(window,paint);}} end{window,&paint};
  RECT client{};if(!GetClientRect(window,&client))return false;Receipt layout;
  const bool drawn=DrawContent(dc,client,context.drawing,text,layout);
  {std::lock_guard lock(state.mutex);state.receipt.client=layout.client;state.receipt.dpi=layout.dpi;
   state.receipt.mark=layout.mark;state.receipt.wordmark=layout.wordmark;state.receipt.status=layout.status;}
  return drawn;
 }
 static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) noexcept {
  auto* context=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  try{
   if(message==WM_NCCREATE){
    context=static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(context));
    std::lock_guard lock(context->state->mutex);context->state->receipt.window=window;return TRUE;
   }
   if(!context)return DefWindowProcW(window,message,wParam,lParam);
   switch(message){
    case WM_ERASEBKGND:return 1;
    case WM_CLOSE:return 0; // Static feedback has no cancellation/close action.
    case WM_PAINT:{
     const bool drawn=Paint(window,*context),visible=IsWindowVisible(window)!=FALSE;
     const bool painted=drawn&&visible;
     if(painted){std::lock_guard lock(context->state->mutex);auto& receipt=context->state->receipt;
      if(!receipt.painted){receipt.painted=true;receipt.paintedAt=Clock();}}
     else if(visible)Failure(*context->state,ERROR_INVALID_DATA);
     if(visible)SetEvent(context->state->ready);return 0;
    }
    case WM_DPICHANGED:{
     const UINT dpi=HIWORD(wParam);const auto rect=Fit(WorkArea(),dpi);
     SetWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
     RECT client{};GetClientRect(window,&client);context->drawing.Build(*context->state,dpi,ContentScale(client));
     InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_NCDESTROY:{
     std::lock_guard lock(context->state->mutex);context->state->receipt.window=nullptr;
     SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
    }
   }
  }catch(...){if(context)Failure(*context->state,ERROR_NOT_ENOUGH_MEMORY);ValidateRect(window,nullptr);return 0;}
  return DefWindowProcW(window,message,wParam,lParam);
 }
 static void Run(State& state) noexcept {
  Context context{&state,{}};HWND window=nullptr;bool registered=false;
  const auto oldDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try{
   {std::lock_guard lock(state.mutex);state.receipt.threadId=GetCurrentThreadId();}
   WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=Procedure;cls.hInstance=state.instance;
   cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=WindowClass;
   registered=RegisterClassExW(&cls)!=0;
   if(!registered)Failure(state,GetLastError());
   else if(state.failCreation)Failure(state,ERROR_NOT_ENOUGH_MEMORY);
   else{
    const auto rect=Fit(WorkArea(),GetDpiForSystem());
    window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,WindowClass,L"NeuRotic",WS_POPUP|WS_BORDER,
        rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,state.instance,&context);
    if(!window)Failure(state,GetLastError());
    else{
     const UINT dpi=GetDpiForWindow(window);const auto fitted=Fit(WorkArea(),dpi);
     SetWindowPos(window,HWND_TOP,fitted.left,fitted.top,fitted.right-fitted.left,fitted.bottom-fitted.top,SWP_NOACTIVATE);
     RECT client{};GetClientRect(window,&client);context.drawing.Build(state,dpi,ContentScale(client));
     ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
     const HANDLE signals[]={state.stop,state.refresh};bool quit=false;
     while(!quit){
      const auto ready=MsgWaitForMultipleObjects(2,signals,FALSE,INFINITE,QS_ALLINPUT);
      if(ready==WAIT_OBJECT_0)break;
      if(ready==WAIT_OBJECT_0+1){InvalidateRect(window,nullptr,FALSE);continue;}
      if(ready==WAIT_FAILED){Failure(state,GetLastError());break;}
      MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
       if(message.message==WM_QUIT){quit=true;break;}TranslateMessage(&message);DispatchMessageW(&message);
      }
     }
    }
   }
  }catch(...){Failure(state,ERROR_NOT_ENOUGH_MEMORY);}
  if(window)DestroyWindow(window);
  const bool released=context.drawing.Release();
  if(registered)UnregisterClassW(WindowClass,state.instance);
  {std::lock_guard lock(state.mutex);state.receipt.gdiReleased=state.cleanupOk&&released;state.receipt.dismissedAt=Clock();}
  SetEvent(state.ready);if(oldDpi)SetThreadDpiAwarenessContext(oldDpi);
 }
 bool StartImpl(HINSTANCE instance,bool failCreation) noexcept {
  Close();
  try{
   {std::lock_guard lock(state_.mutex);state_.receipt={};state_.text=L"Loading...";state_.instance=instance;state_.failCreation=failCreation;state_.cleanupOk=true;}
   ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);refresh_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
   if(!ready_||!stop_||!refresh_){Close();return false;}
   state_.ready=ready_;state_.stop=stop_;state_.refresh=refresh_;
   thread_=std::thread([this](){Run(state_);});
   const auto ready=WaitForSingleObject(ready_,5000);
   const auto receipt=Snapshot();
   if(ready!=WAIT_OBJECT_0||!receipt.painted||!receipt.window||receipt.error){Close();return false;}
   return true;
  }catch(...){Close();return false;}
 }
};
}
