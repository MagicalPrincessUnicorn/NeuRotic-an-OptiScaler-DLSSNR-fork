#include <iostream>
#include <array>
#define NH_KEYBIND_CORE_ONLY
#if __has_include("ui/KeybindCapture.h")
#include "ui/KeybindCapture.h"
#define NH_KEYBIND_AVAILABLE
#endif
int RunKeybindCaptureTests() {
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
#ifdef NH_KEYBIND_AVAILABLE
 using namespace nh;using namespace Neurotic::KeyChord;
 for(auto text:{"auto","-1","0","45","255","0x08","0XFF"})check(Parse(text).has_value(),"legacy decimal/hex/default/unbound values remain valid");
 check(Parse("0x177")==375&&Parse("0x22D")==557&&Parse("846")==846,"modifier chords retain integer representation");
 for(auto text:{"256","0x100","0x800","0x117","0x15B","0x110","-2","1.5","junk","0x7777"})check(!Parse(text),"malformed modifier/base encoding rejected");
 check(Label(45)=="Insert"&&Label(119)=="F8"&&Label(375)=="Ctrl + F8"&&Label(557)=="Shift + Insert"&&Label(846)=="Ctrl + Shift + N","shortcut labels decode familiar names and modifiers");
 KeybindCaptureState capture;std::array<bool,256> down{};down[119]=true;capture.Begin("game-a","nr",down);
 check(capture.listening&&capture.Press(119,0)==std::nullopt,"click begins listening and ignores already-held key");
 capture.Release(119);check(capture.Press(17,0)==std::nullopt&&capture.listening,"modifier alone does not finish capture");
 check(capture.Press(119,Ctrl)==119&&!capture.listening,"next ordinary press captures base key separately from selected modifier");
 capture.Begin("game-a","nr",{});check(!capture.Press(27,0)&&!capture.listening,"Escape cancels without changing draft");
 capture.Begin("game-a","nr",{});capture.Scope("game-b");check(!capture.listening,"changing selected game cancels listening");
 capture.Begin("game-a","nr",{});capture.ResetListening();check(!capture.listening,"tab change/blur resets listening");
 capture.Begin("game-a","nr",{});check(!capture.Press(1,0)&&capture.listening,"mouse press is not a keyboard binding");
 check(!capture.Press(78,0x800)&&capture.listening,"unsupported Windows modifier cannot silently become plain N");
 capture.Begin("game-a","nr",{});check(capture.Press(8,0)==-1&&!capture.listening,"Backspace clears the selected keybind");
 check(Conflicts(119,375)&&Conflicts(375,375)&&!Conflicts(375,631)&&!Conflicts(-1,-1),"conflict detection catches duplicate and overlapping legacy bindings");
 ReleaseTracker tracker;
 tracker.Observe(119,Ctrl,true,false);check(tracker.Observe(119,0,false,true)==375,"runtime release retains modifiers from press even if Ctrl releases first");
 check(Matches(375,375)&&!Matches(375,119)&&!Matches(375,631)&&Matches(119,375),"runtime exact chords and compatible legacy key matching");
 tracker.Observe(119,Ctrl,true,false);tracker.Observe(119,0,true,false);
 check(tracker.Observe(119,0,false,true)==375,"autorepeat after releasing Ctrl retains original key-down chord");
 tracker.Observe(119,Ctrl,true,false);tracker.Reset();check(!tracker.Observe(119,0,false,true),"focus loss clears armed runtime shortcut");
#else
 check(false,"keybind capture and chord policy are available");
#endif
 return failed;
}
#ifdef NH_KEYBIND_STANDALONE
int main(){return RunKeybindCaptureTests();}
#endif
