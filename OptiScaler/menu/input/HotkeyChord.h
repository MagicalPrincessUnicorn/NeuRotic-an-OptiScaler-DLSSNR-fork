#pragma once

#include "../../include/KeyChord.h"

namespace Neurotic::HotkeyChord
{
// UI selectors edit the existing packed key value. Key-only INIs and existing
// runtime matching keep their original representation and behavior.
inline constexpr std::array<int, 8> ModifierValues {
    0, KeyChord::Ctrl, KeyChord::Alt, KeyChord::Ctrl | KeyChord::Shift, KeyChord::Ctrl | KeyChord::Alt,
    KeyChord::Shift, KeyChord::Alt | KeyChord::Shift, KeyChord::Ctrl | KeyChord::Alt | KeyChord::Shift
};
inline constexpr std::array<const char*, 8> ModifierLabels {
    "None", "Ctrl", "Alt", "Ctrl + Shift", "Ctrl + Alt", "Shift", "Alt + Shift", "Ctrl + Alt + Shift"
};

inline int ModifierIndex(int binding)
{
    if (binding == KeyChord::Auto || binding <= 0) return 0;
    if (!KeyChord::Valid(binding)) return -1;
    const int modifiers = binding & KeyChord::ModifierMask;
    for (size_t i = 0; i < ModifierValues.size(); ++i)
        if (modifiers == ModifierValues[i]) return static_cast<int>(i);
    return -1;
}

inline bool ValidModifier(int modifiers)
{
    for (int value : ModifierValues) if (modifiers == value) return true;
    return false;
}

inline int WithModifier(int binding, int modifiers)
{
    if (!ValidModifier(modifiers) || binding <= 0 || !KeyChord::Valid(binding)) return binding;
    const int key = binding & 255;
    if (modifiers && !KeyChord::IsOrdinary(key)) return binding;
    return key | modifiers;
}

inline int WithKey(int binding, int key)
{
    if (!KeyChord::IsOrdinary(key)) return binding;
    const int index = ModifierIndex(binding);
    return key | (index >= 0 ? ModifierValues[index] : 0);
}
}
