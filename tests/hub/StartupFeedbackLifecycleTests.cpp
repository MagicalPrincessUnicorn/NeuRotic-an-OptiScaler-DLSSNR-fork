// Public native HWND/GDI lifecycle check. No App model, D3D, provider or game.
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#if __has_include("ui/StartupFeedback.h")
#include "ui/StartupFeedback.h"
static int failed=0,checks=0;
static void Check(bool good,const char* label){++checks;std::printf("%s %s\n",good?"PASS":"FAIL",label);failed+=!good;}
int main(){
 const auto instance=GetModuleHandleW(nullptr);
 nh::StartupFeedback feedback;
 Check(feedback.Start(instance),"feedback starts before any App initialization");
 const auto shown=feedback.Snapshot();
 DWORD pid=0;const auto tid=GetWindowThreadProcessId(shown.window,&pid);
 Check(shown.window&&IsWindowVisible(shown.window)&&shown.painted,"actual native feedback is visibly painted");
 Check(shown.iconLoaded,"official embedded icon101 was loaded");
 Check(tid&&tid!=GetCurrentThreadId()&&pid==GetCurrentProcessId(),"feedback owns a separate native UI thread in this process");
 HANDLE inert=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Check(inert&&WaitForSingleObject(inert,150)==WAIT_TIMEOUT,"main is held at an inert slow-stage barrier");
 if(inert)CloseHandle(inert);
 DWORD_PTR response=0;
 Check(shown.window&&SendMessageTimeoutW(shown.window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&response)!=0,
       "feedback message loop remains responsive while main initialization is held");
 HWND mainWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned main visibility fixture",WS_OVERLAPPEDWINDOW,
     40,40,320,200,nullptr,nullptr,instance,nullptr);
 Check(mainWindow&&!IsWindowVisible(mainWindow),"real main fixture begins hidden");
 Check(!feedback.DismissWhenMainVisible(mainWindow)&&IsWindowVisible(shown.window),"a hidden main HWND cannot dismiss startup feedback");
 ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
 LARGE_INTEGER mainVisible{};QueryPerformanceCounter(&mainVisible);
 Check(IsWindowVisible(mainWindow)&&feedback.DismissWhenMainVisible(mainWindow),"actual main visibility dismisses feedback");
 const auto closed=feedback.Snapshot();
 Check(closed.threadJoined&&!closed.window&&!IsWindow(shown.window),"successful handoff destroys window and joins its thread");
 Check(closed.gdiReleased,"owned icon/font resources are released after handoff");
 Check(shown.paintedAt>0&&shown.paintedAt<=mainVisible.QuadPart&&closed.dismissedAt>=mainVisible.QuadPart,
       "first feedback paint precedes main visibility which precedes dismissal");
 feedback.Close();Check(feedback.Snapshot().threadJoined,"repeated close is harmless");
 if(mainWindow)DestroyWindow(mainWindow);
 // A scoped early failure must not leave its feedback HWND or kernel handles.
 DWORD before=0,after=0;const bool beforeKnown=GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE;HWND early=nullptr;
 try{nh::StartupFeedback partial;Check(partial.Start(instance),"early-failure feedback starts");early=partial.Snapshot().window;
     throw std::runtime_error("Owned inert startup failure");}catch(const std::runtime_error&){}
 const bool afterKnown=GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE;
 Check(early&&!IsWindow(early),"early exception cleanup destroys the native feedback HWND");
 Check(beforeKnown&&afterKnown&&after==before,"early exception cleanup releases owned event/thread handles");
 nh::StartupFeedback disabled;disabled.Close();Check(!disabled.Snapshot().window,"closing an owner that never started is harmless");

#ifdef NEUROTIC_STARTUP_FEEDBACK_TEST
 nh::StartupFeedback creationFailure;
 Check(!creationFailure.StartWithCreationFailureForTest(instance),"feedback creation failure returns without blocking App startup");
 const auto rejected=creationFailure.Snapshot();
 Check(rejected.error!=0&&!rejected.window&&rejected.threadJoined&&rejected.gdiReleased,"creation failure joins its thread and releases owned resources");
#endif
 std::printf("CHECKS=%d FAILURES=%d\n",checks,failed);return failed?1:0;
}
#else
int main(){std::puts("FAIL no startup feedback owner exists before slow desktop initialization\nCHECKS=1 FAILURES=1");return 1;}
#endif
