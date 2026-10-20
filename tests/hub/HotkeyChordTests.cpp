#include "../../OptiScaler/include/KeyChord.h"
#include "../../OptiScaler/menu/input/HotkeyChord.h"
#include <iostream>
int main()
{
    int failed = 0;
    auto check = [&](bool ok, const char* label) {
        std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
        failed += !ok;
    };
    using namespace Neurotic;
    struct Case { int modifiers, chord; };
    // Editing a selector recombines the existing packed authority; key-only INIs stay valid.
    for (const Case c : { Case{0,119}, Case{256,375}, Case{512,631}, Case{1024,1143}, Case{768,887}, Case{1280,1399}, Case{1536,1655}, Case{1792,1911} })
    {
        check(HotkeyChord::WithKey(c.chord,120)==(120|c.modifiers), "changing base key preserves every valid modifier combination");
        check(HotkeyChord::ModifierIndex(c.chord)>=0, "supported packed modifier choice shown in editor");
        check(HotkeyChord::WithModifier(119,c.modifiers)==c.chord, "separate modifier selector retains unchanged F8 base");
        check(KeyChord::Matches(HotkeyChord::WithModifier(119,c.modifiers),c.chord), "chosen chord activates F8");
        if(c.modifiers) check(!KeyChord::Matches(HotkeyChord::WithModifier(119,c.modifiers),119), "plain F8 does not activate modified binding");
    }
    for (int unsupported : {2048}) {
        check(HotkeyChord::ModifierIndex(119|unsupported)==-1, "unsupported modifier bits stay outside selectable choices");
        check(HotkeyChord::WithModifier(119,unsupported)==119, "unsupported new modifier selection preserves saved binding");
    }
    check(HotkeyChord::WithKey(375,120)==376, "base key change preserves selected Ctrl modifier");
    check(HotkeyChord::WithModifier(375,1024)==1143, "explicit Alt choice replaces saved Ctrl modifier");
    check(HotkeyChord::WithModifier(-1,256)==-1 && HotkeyChord::WithModifier(0,256)==0,
          "unbound and disabled keys cannot become chords");
    check(HotkeyChord::WithKey(375,91)==375 && HotkeyChord::WithKey(375,16)==375,
          "Windows and modifier-alone key capture preserves saved binding");
    check(HotkeyChord::WithModifier(631,256)==375, "legacy Shift-only binding can be explicitly changed to supported Ctrl");
    KeyChord::ReleaseTracker tracker;
    tracker.Observe(119,KeyChord::Ctrl|KeyChord::Shift,true,false);
    const auto released=tracker.Observe(119,0,false,true);
    check(released && KeyChord::Matches(HotkeyChord::WithModifier(119,768),*released),
          "modifier release before key still dispatches chosen chord");
    return failed;
}
