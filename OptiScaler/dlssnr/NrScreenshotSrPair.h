#pragma once

// Only temporary screenshot features may use this helper. Both images use the
// same current-frame guides and a reset history; the live feature is never run
// or reset here. Restore every parameter override, including failure/exception.
namespace DlssNr::Screenshots
{
template<class Parameters, class Resource, class Evaluate>
bool FreshSrPair(Parameters* params, Resource* before, Resource* after,
                 Resource* outputBefore, Resource* outputAfter, Evaluate evaluate)
{
    Resource* savedColor = nullptr;
    Resource* savedOutput = nullptr;
    int savedReset = 0;
    if (!params || !before || !after || !outputBefore || !outputAfter ||
        outputBefore == outputAfter || before == outputBefore || before == outputAfter ||
        after == outputBefore || after == outputAfter ||
        params->Get("Color", &savedColor) != 1 ||
        params->Get("Output", &savedOutput) != 1 ||
        params->Get("Reset", &savedReset) != 1 || !savedColor || !savedOutput ||
        savedOutput == outputBefore || savedOutput == outputAfter)
        return false;
    struct Restore
    {
        Parameters* params;
        Resource* color;
        Resource* output;
        int reset;
        ~Restore()
        {
            params->Set("Color", color);
            params->Set("Output", output);
            params->Set("Reset", reset);
        }
    } restore {params, savedColor, savedOutput, savedReset};
    params->Set("Color", before);
    params->Set("Output", outputBefore);
    params->Set("Reset", 1);
    if (!evaluate()) return false;
    params->Set("Color", after);
    params->Set("Output", outputAfter);
    params->Set("Reset", 1);
    return evaluate();
}
}
