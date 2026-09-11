#pragma once

#include "../SynchronizedOptional.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DlssNr
{
// UI-only preview state. No option is written until Commit; the same transaction covers
// every child so a rendering snapshot cannot observe half of a shared edit.
class NrPendingEdit
{
    std::vector<NrOptional<float>*> _targets;
    std::vector<float> _original;
    float _preview = 0.0f;
    int _lastFrame = -1;
    uint64_t _profileGeneration = 0;
    bool _pending = false;
    bool _blocked = false;

    bool Matches() const
    {
        for (size_t i = 0; i < _targets.size(); ++i)
            if (_targets[i]->value_or_default() != _original[i]) return false;
        return true;
    }

  public:
    void Prepare(const std::vector<NrOptional<float>*>& targets, int frame)
    {
        NrConfigSynchronization::Transaction transaction;
        const auto generation = NrConfigSynchronization::ProfileGeneration();
        if (_pending && (targets != _targets || frame != _lastFrame + 1 ||
                         generation != _profileGeneration || !Matches()))
            Cancel();
        _targets = targets;
        _profileGeneration = generation;
        _lastFrame = frame;
    }
    float Value() const { return _pending ? _preview : _targets.front()->value_or_default(); }
    bool Mixed() const
    {
        NrConfigSynchronization::Transaction transaction;
        const float first = _targets.front()->value_or_default();
        return std::any_of(_targets.begin() + 1, _targets.end(), [first](auto* option)
        { return option->value_or_default() != first; });
    }
    void Preview(float value)
    {
        if (_blocked || !std::isfinite(value)) return;
        if (!_pending)
        {
            NrConfigSynchronization::Transaction transaction;
            _original.clear();
            for (auto* option : _targets) _original.push_back(option->value_or_default());
            _profileGeneration = NrConfigSynchronization::ProfileGeneration();
        }
        _preview = value;
        _pending = true;
    }
    bool Commit(float minimum, float maximum)
    {
        NrConfigSynchronization::Transaction transaction;
        if (!_pending || _blocked || _profileGeneration != NrConfigSynchronization::ProfileGeneration() || !Matches())
        { Cancel(); return false; }
        const float value = std::clamp(_preview, minimum, maximum);
        bool changed = false;
        for (auto* option : _targets)
            if (option->value_or_default() != value) { *option = value; changed = true; }
        _pending = false;
        return changed;
    }
    void Reset(float value)
    {
        NrConfigSynchronization::Transaction transaction;
        for (auto* option : _targets) *option = value;
        Cancel();
    }
    void Cancel() { _pending = false; _blocked = true; }
    void Finish(bool active) { if (!active) { _pending = false; _blocked = false; } }
};
}
