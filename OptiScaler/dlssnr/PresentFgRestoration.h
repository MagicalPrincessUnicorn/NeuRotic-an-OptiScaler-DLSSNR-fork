#pragma once
#include "PresentFgScope.h"

namespace DlssNr::PreFg::FinalColor
{
enum class Declaration { Omitted, Removed, Present, Unknown };
struct OptionalDeclarations
{
    Declaration hudless = Declaration::Omitted, color = Declaration::Omitted, alpha = Declaration::Omitted;
};
enum class FreshPolicy { Pending, InputsAccepted, SeparatedAlpha, SeparatedColor, HudlessOnly, OptionalCleared };

// Status facts only, serialized by the caller's policy bookkeeping boundary.
// No native resource, extent, pixel state, temporal history or lifetime promise.
// AcceptedOriginal is called ONLY for a successful unfiltered actual call after
// the real exit reset and old filtered-call drain, with its entry/return scope
// still current. Failed/filtered calls and cached game intent never enter here.
class Restoration
{
    Scope scope_ {};
    bool scoped_ = false, exiting_ = false;
    uint64_t frame_ = 0;
    bool legacy_ = true;
    unsigned known_ = 0, present_ = 0, game_ = 0;

    void Apply(unsigned bit, Declaration value)
    {
        if (value == Declaration::Omitted) return;
        if (value == Declaration::Unknown) { known_ &= ~bit; present_ &= ~bit; game_ &= ~bit; return; }
        known_ |= bit; game_ |= bit;
        if (value == Declaration::Present) present_ |= bit;
        else present_ &= ~bit;
    }
public:
    void ChangeScope(const Scope& scope)
    {
        if (scoped_ && scope_ == scope) return;
        // Keep only actual absence facts in the same provider/viewport domain.
        // A route/resume/output counter alone cannot recreate a registration.
        const bool sameDomain = scoped_ && scope_.provider == scope.provider && scope_.viewport == scope.viewport;
        known_ = sameDomain ? known_ & ~present_ : 0;
        present_ = game_ = 0;
        scope_ = scope; scoped_ = true;
    }
    void ClearAccepted(const Scope& scope, uint64_t frame = 0, bool legacy = true)
    {
        scope_ = scope; scoped_ = true; exiting_ = false;
        frame_ = frame; legacy_ = legacy;
        known_ = 7; present_ = 0; game_ = 0;
    }
    void ExitRequested(const Scope& scope)
    {
        if (!(scoped_ && scope_ == scope))
        {
            ChangeScope(scope);
        }
        exiting_ = true;
    }
    void AcceptedOriginal(const Scope& scope, OptionalDeclarations declaration, uint64_t frame = 0, bool legacy = true)
    {
        if (!scoped_ || !exiting_ || !(scope_ == scope)) return;
        if (legacy != legacy_ || (!legacy && frame != frame_))
        {
            // A prior token's clear is not evidence of this token's absence.
            // Do not union H from one token with UI from another token.
            known_ = present_ = game_ = 0;
            frame_ = frame; legacy_ = legacy;
        }
        Apply(1, declaration.hudless); Apply(2, declaration.color); Apply(4, declaration.alpha);
    }
    void UnknownOriginal(const Scope& scope)
    {
        if (scoped_ && scope_ == scope) known_ = present_ = game_ = 0;
    }
    FreshPolicy Read(const Scope& scope) const
    {
        if (!scoped_ || !exiting_ || !(scope_ == scope)) return FreshPolicy::Pending;
        const bool freshH = (game_ & present_ & 1) != 0;
        const bool alpha = (known_ & present_ & 4) != 0;
        const bool alphaAbsent = (known_ & 4) && !(present_ & 4);
        const bool color = (known_ & present_ & 2) != 0;
        const bool colorAbsent = (known_ & 2) && !(present_ & 2);
        if (freshH && alpha && (game_ & 4)) return FreshPolicy::SeparatedAlpha;
        if (freshH && color && (game_ & 2) && alphaAbsent) return FreshPolicy::SeparatedColor;
        // Accepted H+C does not establish whether an unobserved A alternative
        // is absent/selected. It still is actual fresh input acceptance.
        if (freshH && color && (game_ & 2)) return FreshPolicy::InputsAccepted;
        if (freshH && alphaAbsent && colorAbsent && (game_ & 6) == 6) return FreshPolicy::HudlessOnly;
        if (known_ == 7 && game_ == 7 && present_ == 0) return FreshPolicy::OptionalCleared;
        return FreshPolicy::Pending;
    }
};
}
