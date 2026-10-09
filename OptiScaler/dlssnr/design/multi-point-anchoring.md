# Exposure anchor mapping

Exposure anchors map a scanned value to a chosen white point. With no anchors, retain the existing manual or single-anchor path. One anchor uses whitePoint = anchorWhitePoint * anchorValue / scanValue in normal mode, or anchorWhitePoint * scanValue / anchorValue in inverted mode. Apply trim and clamp the resulting white point to 0.01–4096 in all cases.

For multiple anchors, sort positive scan values and interpolate the positive white points in logarithmic space between adjacent anchors:

    t = (log(scanValue) - log(v0)) / (log(v1) - log(v0))
    whitePoint = exp((1 - t) * log(w0) + t * log(w1))

Clamp outside the calibrated range to the nearest endpoint. Reject invalid values and preserve the existing fallback. Continuous re-anchoring would erase the measured relationship; calibration requires explicit anchor choices.
