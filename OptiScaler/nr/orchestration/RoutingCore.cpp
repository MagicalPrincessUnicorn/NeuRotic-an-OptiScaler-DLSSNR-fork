#include "RoutingPublication.h"
#include "LegacyShadowComparison.h"
// Compile the pure policy surface in the product build; no live route is connected here.
static_assert(Neurotic::Orchestration::PolicyVersion == 1);
