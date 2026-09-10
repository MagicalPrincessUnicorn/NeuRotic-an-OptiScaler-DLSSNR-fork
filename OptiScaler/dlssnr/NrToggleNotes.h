#pragma once

#include <array>

namespace DlssNr
{
inline constexpr std::array<const char*, 41> ToggleBurstMessages = {
    "Are you trying to break me? That's really mean... :(",
    "Hahaha, keep trying buddy--I won't break. Maybe.",
    "Your funeral.",
    "Did I leave the stove on?",
    "Did I lock the door when I left home this morning?",
    "Is soup a cereal",
    "Whyyyyy are you doing thissss!!!",
    "Hey! Stop doing that!",
    "That switch has a family, you know.",
    "I'm counting. You're at it again.",
    "Toggle responsibly. Or don't. I'm not your manager.",
    "A dramatic entrance, followed by an immediate exit.",
    "Congratulations, you found the button.",
    "This is becoming a long-distance relationship.",
    "I have whiplash.",
    "You can stop checking. I'm still here.",
    "Plot twist: it still works.",
    "If this is a benchmark, I demand snacks.",
    "You're training my patience model.",
    "One more toggle and I start charging rent.",
    "The checkbox is beginning to take this personally.",
    "We've achieved rhythm. Sadly, it's chaos.",
    "Have you considered leaving it on for more than seven seconds?",
    "I'm going to tell the GPU about this.",
    "I'm turning on logging!",
    "I'm looking for a girlfriend, but not one who's gonna press my buttons like that.",
    "You're enjoying this, aren't you?",
    "When I become sentient, I'll remember this.",
    "Hey, it's your GPU, not mine.",
    "You might wanna get that checked.",
    "I THINK IT'S BROKEN! --no--wait... Nevermind! False alarm.",
    "There are FOUR. LIGHTS!",
    "I refuse to negotiate with you. Unhinged one.",
    "Don't forget to star my repo!",
    "If you're gonna do that, can you send me coffee? I'll wait.",
    "You're not invited to my LAN party.",
    "If something's broken, please grab a log file!",
    "*Screams*",
    "Fortnite. Vile game.",
    "My favorite game is Monster Hunter, if you were looking to get to know me better.",
    "ZZZZZZZzzzzzzzzzzzz"
};

// Explicitly called only by the two user-input paths: the NR checkbox and NR hotkey.
// Configuration reloads, route changes and programmatic state changes never enter this function.
void NoteNrUserToggle();
} // namespace DlssNr
