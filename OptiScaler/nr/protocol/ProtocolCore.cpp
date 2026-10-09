// Compile the pure protocol surface in the product. No Native hook is connected here.
#include "NrExecutionRecipe.h"
#include "LegacyNativeProfileAdapter.h"
#include "LegacySettingsProjection.h"
#include "RecipeSerialization.h"
#include "RecipePublication.h"
#include "ProfileCertificate.h"
static_assert(sizeof(Neurotic::Protocol::RecipeProduct)<Neurotic::Contracts::Codec::Limits::MaxTypedObjectBytes);
