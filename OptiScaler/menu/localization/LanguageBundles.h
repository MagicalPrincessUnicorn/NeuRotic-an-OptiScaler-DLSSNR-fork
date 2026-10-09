#pragma once
#include "LanguagePack.h"
namespace Neurotic::Localization {
const std::vector<LanguagePack>& IncludedPacks();
// Immutable bundled data is validated once; user files must still be validated.
const PackValidation* IncludedPackValidation(std::string_view id);
const LanguagePack* IncludedPack(std::string_view id);
const LanguagePack* ApplicableBundled(std::string_view locale);
}
