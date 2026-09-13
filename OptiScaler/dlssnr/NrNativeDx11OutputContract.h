#pragma once

namespace DlssNr::NativeDx11
{
enum class OutputContractResult
{
    Accepted,
    PartialOrUnsupported,
    PresentTargetMismatch,
};

inline OutputContractResult ValidateOutputContract(bool nativeRoute, unsigned int outputX,
    unsigned int outputY, bool outputBackingMatches, bool supportedShape,
    unsigned int outputWidth, unsigned int outputHeight,
    unsigned int presentWidth, unsigned int presentHeight)
{
    if (outputX != 0 || outputY != 0 || !outputBackingMatches || !supportedShape)
        return OutputContractResult::PartialOrUnsupported;
    if (!nativeRoute && (outputWidth != presentWidth || outputHeight != presentHeight))
        return OutputContractResult::PresentTargetMismatch;
    return OutputContractResult::Accepted;
}
}
