#pragma once

namespace Neurotic::MenuConfig
{
// Retired keys are ignored regardless of their old value. This only changes
// the loaded document; disk writes still require the existing save transaction.
template<class Ini> void DiscardRetiredKeys(Ini& ini)
{
    ini.Delete("Menu", "OptiClip");
}
}
