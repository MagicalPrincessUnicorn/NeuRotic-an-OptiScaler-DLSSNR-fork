#include "anything/AnythingHotkey.h"
#include "anything/AnythingPreferences.h"
#include <iostream>
int main(){
 int failures=0;auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failures+=!ok;};
 nh::AnythingUiState p;p.preferencesLoaded=true;nh::AnythingHotkey hotkey;
 check(p.hotkeyEnabled&&p.hotkeyModifiers==(MOD_CONTROL|MOD_ALT)&&p.hotkeyKey=='N',"Default shortcut is Ctrl+Alt+N");
 check(p.screenshotHotkeyEnabled&&p.screenshotHotkeyModifiers==(MOD_CONTROL|MOD_ALT)&&p.screenshotHotkeyKey=='S',"Default screenshot shortcut is Ctrl+Alt+S");
 check(!nh::ValidAnythingHotkey(MOD_CONTROL|MOD_ALT,VK_F10)&&!nh::ValidAnythingHotkey(0,'N'),"Emergency stop and unmodified keys cannot be assigned");
 bool conflict=RegisterHotKey(nullptr,0x3af1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'N')!=FALSE;
 hotkey.Update(nullptr,p);check(!p.hotkeyAvailable&&!p.hotkeyError.empty(),"Existing system registration is reported as unavailable");
 if(conflict)UnregisterHotKey(nullptr,0x3af1);
 p.hotkeyKey=VK_F9;p.hotkeyModifiers=MOD_CONTROL|MOD_SHIFT;hotkey.Update(nullptr,p);
 check(p.hotkeyAvailable&&p.hotkeyError.empty(),"Changing shortcut registers available combination");
 check(hotkey.Accept(nh::AnythingHotkey::Id,MAKELPARAM(p.hotkeyModifiers,p.hotkeyKey),p),"Registered shortcut message is accepted");
 check(!hotkey.Accept(nh::AnythingHotkey::Id,MAKELPARAM(MOD_CONTROL|MOD_ALT,'N'),p),"Queued old binding cannot toggle after rebinding");
 p.hotkeyEnabled=false;hotkey.Update(nullptr,p);check(!p.hotkeyAvailable&&!hotkey.Accept(nh::AnythingHotkey::Id,MAKELPARAM(p.hotkeyModifiers,p.hotkeyKey),p),"Disabled shortcut cannot toggle");
 bool reusable=RegisterHotKey(nullptr,0x3af1,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,VK_F9)!=FALSE;check(reusable,"Disable releases shortcut ownership");if(reusable)UnregisterHotKey(nullptr,0x3af1);
 p.hotkeyEnabled=true;p.screenshotHotkeyModifiers=MOD_CONTROL|MOD_SHIFT;p.screenshotHotkeyKey=VK_F8;hotkey.Update(nullptr,p);
 check(p.hotkeyAvailable&&p.screenshotHotkeyAvailable&&hotkey.AcceptScreenshot(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(p.screenshotHotkeyModifiers,p.screenshotHotkeyKey),p),"Rendering and screenshot have independently registered message IDs");
 p.hotkeyKey=VK_F8;check(!hotkey.AcceptScreenshot(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(p.screenshotHotkeyModifiers,p.screenshotHotkeyKey),p),"A newly colliding rendering edit immediately refuses screenshot events before update");p.hotkeyKey=VK_F9;
 check(!hotkey.Accept(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(p.screenshotHotkeyModifiers,p.screenshotHotkeyKey),p)&&!hotkey.AcceptScreenshot(nh::AnythingHotkey::Id,MAKELPARAM(p.hotkeyModifiers,p.hotkeyKey),p),"Screenshot and rendering events cannot cross-dispatch");
 ++p.screenshotHotkeyRevision;check(!hotkey.AcceptScreenshot(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(p.screenshotHotkeyModifiers,p.screenshotHotkeyKey),p),"Pending screenshot registration revision refuses stale events");hotkey.Update(nullptr,p);
 p.screenshotHotkeyKey=VK_F7;hotkey.Update(nullptr,p);check(!hotkey.AcceptScreenshot(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(MOD_CONTROL|MOD_SHIFT,VK_F8),p),"Rebinding screenshots rejects queued old binding");
 p.screenshotHotkeyKey=p.hotkeyKey;hotkey.Update(nullptr,p);check(p.hotkeyAvailable&&!p.screenshotHotkeyAvailable&&!p.screenshotHotkeyError.empty(),"Duplicate shortcut preserves rendering and explains screenshot conflict");
 p.hotkeyEnabled=false;hotkey.Update(nullptr,p);check(p.screenshotHotkeyAvailable,"Disabling rendering releases its combination for screenshots");
 p.screenshotHotkeyEnabled=false;hotkey.Update(nullptr,p);check(!p.screenshotHotkeyAvailable&&!hotkey.AcceptScreenshot(nh::AnythingHotkey::ScreenshotId,MAKELPARAM(p.screenshotHotkeyModifiers,p.screenshotHotkeyKey),p),"Disabled screenshot binding cannot capture");
 p.screenshotHotkeyEnabled=true;p.screenshotHotkeyModifiers=MOD_CONTROL|MOD_ALT;p.screenshotHotkeyKey=VK_F10;hotkey.Update(nullptr,p);check(!p.screenshotHotkeyAvailable&&!p.screenshotHotkeyError.empty(),"Screenshot binding cannot take the emergency Stop combination");
 bool screenshotConflict=RegisterHotKey(nullptr,0x3af2,MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F8)!=FALSE;p.screenshotHotkeyModifiers=MOD_ALT|MOD_SHIFT;p.screenshotHotkeyKey=VK_F8;hotkey.Update(nullptr,p);check(!p.screenshotHotkeyAvailable&&!p.screenshotHotkeyError.empty(),"Screenshot OS registration conflict is reported independently");if(screenshotConflict)UnregisterHotKey(nullptr,0x3af2);
 ++p.screenshotHotkeyRevision;hotkey.Update(nullptr,p);check(p.screenshotHotkeyAvailable,"Screenshot retry registers after conflicting owner releases binding");
 p.screenshotHotkeyModifiers=MOD_ALT|MOD_SHIFT;p.screenshotHotkeyKey=VK_F8;p.screenshotHotkeyEnabled=false;
 auto path=std::filesystem::current_path()/L"builds/hub-anything/hotkey-fixture.json";nh::SaveAnythingPreferences(p,path);nh::AnythingUiState restored;nh::LoadAnythingPreferences(restored,path);check(!restored.hotkeyEnabled&&restored.hotkeyKey==VK_F9&&restored.hotkeyModifiers==(MOD_CONTROL|MOD_SHIFT),"Configured shortcut survives preferences roundtrip");
 check(!restored.screenshotHotkeyEnabled&&restored.screenshotHotkeyModifiers==(MOD_ALT|MOD_SHIFT)&&restored.screenshotHotkeyKey==VK_F8,"Screenshot shortcut independently survives preferences roundtrip");
 nlohmann::json legacy;{std::ifstream in(path);legacy=nlohmann::json::parse(in);}legacy.erase("screenshotHotkey");{std::ofstream out(path);out<<legacy.dump();}nh::AnythingUiState upgraded;nh::LoadAnythingPreferences(upgraded,path);check(upgraded.screenshotHotkeyEnabled&&upgraded.screenshotHotkeyKey=='S'&&upgraded.screenshotHotkeyModifiers==(MOD_CONTROL|MOD_ALT)&&!upgraded.hotkeyEnabled,"Old preferences acquire screenshot defaults without changing rendering choice");std::filesystem::remove(path);
 return failures?1:0;
}
