#pragma once
#include <cmath>

namespace Neurotic::Sleek
{
inline constexpr float RevealDuration = .18f;
// Constant velocity with an exact endpoint, so reveal motion has no easing tail.
inline float LinearStep(float current, float target, float elapsed, float speed)
{
    if(!std::isfinite(target)) target=0;
    if(!std::isfinite(current)) return target;
    if(!std::isfinite(elapsed) || elapsed<=0) return current;
    if(elapsed>=.25f || !std::isfinite(speed) || speed<=0) return target;
    const float distance=target-current,step=speed*elapsed;
    if(std::abs(distance)<=step) return target;
    return current+(distance<0?-step:step);
}
// Exponential response: the same elapsed time produces the same motion at any frame rate.
inline float Approach(float current, float target, float elapsed, float rate = 18.0f)
{
    if (!std::isfinite(target)) target = 0.0f;
    if (!std::isfinite(current)) return target;
    if (!std::isfinite(elapsed) || elapsed <= 0.0f) return current;
    if (elapsed >= 0.25f) return target;
    if (!std::isfinite(rate) || rate <= 0.0f) return target;
    const float result = current + (target-current) * (1.0f-std::exp(-rate*elapsed));
    return std::abs(target-result) < 0.0001f ? target : result;
}
}
