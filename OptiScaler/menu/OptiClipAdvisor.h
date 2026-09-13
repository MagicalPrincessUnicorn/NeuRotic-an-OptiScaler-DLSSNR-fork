#pragma once

#include "OptiClipController.h"

namespace OptiClip
{
enum class ToggleOrigin : unsigned char { Checkbox, Hotkey };

struct Bounds
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

void SetEnabled(bool enabled, double nowSeconds);
void Interaction(double nowSeconds);
void ReportRapidToggle(ToggleOrigin origin, const char* message, double nowSeconds);
void ObserveThreshold(const char* identity, Event event, double value, double boundary,
                      bool committed, double nowSeconds);
void Render(const Bounds& menu, float menuScale, double nowSeconds, bool enabled);
} // namespace OptiClip
