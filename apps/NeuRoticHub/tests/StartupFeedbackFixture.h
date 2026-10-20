#pragma once
// Inert checkpoints exist only in the explicitly compiled local native fixture.
// Shipping/default App builds neither read these inputs nor wait on events.
#ifdef NEUROTIC_STARTUP_FEEDBACK_TEST
#include "../ui/StartupTimingProbe.h"
#include <windows.h>
#include <cwchar>
#include <stdexcept>
#include <string>

namespace nh::startup_fixture {
inline void Checkpoint(const wchar_t* stage) {
 wchar_t selected[64]{},token[65]{},action[16]{};
 const auto selectedSize=GetEnvironmentVariableW(L"NEUROTIC_STARTUP_TEST_STAGE",selected,64);
 if(!selectedSize)return;
 if(selectedSize>=64)throw std::runtime_error("Startup fixture stage exceeds its limit");
 if(std::wcscmp(stage,selected)!=0)return;
 const auto tokenSize=GetEnvironmentVariableW(L"NEUROTIC_STARTUP_TEST_TOKEN",token,65);
 const auto actionSize=GetEnvironmentVariableW(L"NEUROTIC_STARTUP_TEST_ACTION",action,16);
 if(!startup_probe::Active()||!startup_probe::Offline()||!tokenSize||tokenSize>=65||!actionSize||actionSize>=16)
  throw std::runtime_error("Startup fixture requires the isolated offline timing profile and owned event token");
 for(DWORD i=0;i<tokenSize;++i)if(!((token[i]>=L'0'&&token[i]<=L'9')||(token[i]>=L'a'&&token[i]<=L'z')||(token[i]>=L'A'&&token[i]<=L'Z')||token[i]==L'-'))
  throw std::runtime_error("Startup fixture event token is invalid");
 if(std::wcscmp(action,L"barrier")!=0&&std::wcscmp(action,L"error")!=0)
  throw std::runtime_error("Startup fixture action is invalid");
 const auto prefix=std::wstring(L"Local\\NeuRotic-StartupFixture-")+token;
 HANDLE entered=OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"-Entered").c_str());
 HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"-Release").c_str());
 struct Close { HANDLE value;~Close(){if(value)CloseHandle(value);} } closeEntered{entered},closeRelease{release};
 if(!entered||!release)throw std::runtime_error("Startup fixture owned events are unavailable");
 startup_probe::Mark("startup_test_checkpoint_begin");
 if(!SetEvent(entered))throw std::runtime_error("Startup fixture entry could not be recorded");
 if(std::wcscmp(action,L"error")==0){startup_probe::Mark("startup_test_checkpoint_error");throw std::runtime_error("Controlled startup initialization failure");}
 const auto waited=WaitForSingleObject(release,60000);
 startup_probe::Mark("startup_test_checkpoint_end",waited);
 if(waited!=WAIT_OBJECT_0)throw std::runtime_error("Startup fixture barrier did not complete normally");
}
}
#else
namespace nh::startup_fixture { inline void Checkpoint(const wchar_t*) noexcept {} }
#endif
