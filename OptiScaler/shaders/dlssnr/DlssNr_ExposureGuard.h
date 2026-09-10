#pragma once

#include <cmath>

namespace DlssNr::ExposureGuard
{
inline bool IsSupportedWhitePoint(float exposure, float preExposure, float trim, float& whitePoint)
{
    whitePoint = preExposure / exposure * trim;
    return std::isfinite(whitePoint) && whitePoint >= 0.01f && whitePoint <= 4096.0f;
}

class WhitePointHold
{
  public:
    float Resolve(float candidate, bool supported, float manualWhitePoint)
    {
        if (supported)
        {
            _value = candidate;
            _valid = true;
        }

        return _valid ? _value : manualWhitePoint;
    }

    void Reset()
    {
        _value = 0.0f;
        _valid = false;
    }

    bool HasValue() const { return _valid; }
    float Value() const { return _value; }

  private:
    float _value = 0.0f;
    bool _valid = false;
};
}
