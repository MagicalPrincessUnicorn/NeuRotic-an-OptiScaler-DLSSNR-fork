#pragma once

namespace OptiInput
{
struct MenuInputPolicy
{
    bool allowMouse = false;
    bool allowKeyboard = true;
    bool allowController = true;
    bool captureKey = false;
    constexpr bool Mouse(bool visible) const { return visible && (!allowMouse || captureKey); }
    constexpr bool Keyboard(bool visible) const { return visible && (!allowKeyboard || captureKey); }
    constexpr bool Controller(bool visible) const { return visible && (!allowController || captureKey); }
};

// A press owned by a popup, text editor or key assignment must not close its parent on release.
struct EscapeCloseGesture
{
    bool armed = false;
    bool Update(bool eligible, bool pressed, bool released)
    {
        if (!eligible) armed = false;
        if (pressed) armed = eligible;
        const bool close = released && armed;
        if (released) armed = false;
        return close;
    }
};
}
