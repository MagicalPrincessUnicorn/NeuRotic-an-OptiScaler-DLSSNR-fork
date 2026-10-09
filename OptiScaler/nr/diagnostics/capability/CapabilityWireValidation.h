#pragma once
#include "../../../../external/nlohmann/json.hpp"
namespace DlssNr::Capability::Wire {
using Json=nlohmann::json;
Json Parse(std::string_view);
bool Shape(const Json&);
bool Semantics(const Json&);
bool LegacyShape(const Json&);
}
