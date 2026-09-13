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
}
