#pragma once
#include "LanguageCatalog.h"
namespace Neurotic::Localization {
struct LanguageIssue{std::string code;NamedArguments parameters;std::string English()const;};
LanguageIssue TranslationIssue(std::string_view,const EnglishEntry&);
struct EntryError{std::string id,reason,reasonCode;NamedArguments parameters;};
struct PackValidation{bool accepted=false;LanguagePack pack;std::vector<EntryError> errors;std::string error,errorCode;NamedArguments errorParameters;};
PackValidation ValidatePack(std::string_view json,const EnglishCatalog&);
bool ValidLocale(std::string_view locale);
std::string TranslationError(std::string_view text,const EnglishEntry& entry);
}
