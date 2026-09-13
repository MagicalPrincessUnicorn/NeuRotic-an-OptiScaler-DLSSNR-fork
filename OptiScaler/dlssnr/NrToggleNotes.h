#pragma once

#include <array>
#include <menu/OptiClipAdvisor.h>

namespace DlssNr
{
inline constexpr std::array<const char*, 27> ToggleBurstMessages = {
    "Hahaha, keep trying buddy--I won't break. Maybe.",
    "Whyyyyy are you doing thissss!!!",
    "Hey! Stop doing that!",
    "That switch has a family, you know.",
    "I'm counting. You're at it again.",
    "Toggle responsibly. Or don't. I'm not your manager.",
    "A dramatic entrance, followed by an immediate exit.",
    "Congratulations, you found the button.",
    "I have whiplash.",
    "You can stop checking. I'm still here.",
    "Plot twist: it still works.",
    "If this is a benchmark, I demand snacks.",
    "You're training my patience model.",
    "One more toggle and I start charging rent.",
    "The checkbox is beginning to take this personally.",
    "We've achieved rhythm. Sadly, it's chaos.",
    "Have you considered leaving it on for more than seven seconds?",
    "You're enjoying this, aren't you?",
    "Hey, it's your GPU, not mine.",
    "You might wanna get that checked.",
    "I THINK IT'S BROKEN! --no--wait... Nevermind! False alarm.",
    "I refuse to negotiate with you. Unhinged one.",
    "You're not invited to my LAN party.",
    "You have discovered both available positions.",
    "Please choose a reality and remain in it.",
    "At this rate the checkbox deserves overtime.",
    "On. Off. On. Off. Excellent diagnostic methodology."
};

// Explicitly called only by the two user-input paths: the NR checkbox and NR hotkey.
// Configuration reloads, route changes and programmatic state changes never enter this function.
void NoteNrUserToggle(OptiClip::ToggleOrigin origin);
} // namespace DlssNr
