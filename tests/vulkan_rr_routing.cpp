#include "../OptiScaler/inputs/VulkanRrRouting.h"

#include <cstdio>
#include <cstdlib>

using namespace VulkanRrRouting;

static void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Vulkan RR routing assertion failed: %s\n", message);
        std::abort();
    }
}

static void SelectedRouteAndAuxiliaryBypass()
{
    Registry routes;
    routes.Register(1000000, 1, 1, { 3440, 1440 });
    routes.Register(1000001, 1, 2, { 1146, 480 });
    Require(routes.Observe(1000000, 1, { 3440, 1440 }), "main observation");
    Require(routes.Observe(1000001, 2, { 1146, 480 }), "aux observation");
    const auto auxiliary = routes.Select(1000001, 1, 2, 10, { 3440, 1440 });
    Require(!auxiliary.execute && auxiliary.reason == BypassReason::Auxiliary &&
                auxiliary.selectedHandle == 1000000,
            "auxiliary route bypass");
    const auto main = routes.Select(1000000, 1, 1, 10, { 3440, 1440 });
    Require(main.execute && main.reason == BypassReason::None && main.executionCount == 1, "main route selected");
}

static void AmbiguityAndCorrelationFailClosed()
{
    Registry routes;
    routes.Register(10, 1, 1, { 3440, 1440 });
    routes.Register(11, 1, 2, { 3440, 1440 });
    routes.Observe(10, 1, { 3440, 1440 });
    routes.Observe(11, 2, { 3440, 1440 });
    Require(routes.Select(10, 1, 1, 1, { 3440, 1440 }).reason == BypassReason::Ambiguous,
            "ambiguous match bypass");
    Require(routes.Select(10, 1, UnknownViewport, 1, { 3440, 1440 }).reason == BypassReason::Uncorrelated,
            "uncorrelated bypass");
    Require(routes.Select(10, 1, 2, 1, { 3440, 1440 }).reason == BypassReason::ViewportMismatch,
            "viewport mismatch bypass");
}

static void DuplicateReleaseAndContractChange()
{
    Registry routes;
    routes.Register(10, 1, 1, { 3440, 1440 });
    routes.Observe(10, 1, { 3440, 1440 });
    Require(routes.Select(10, 1, 1, 50, { 3440, 1440 }).execute, "initial execution");
    Require(routes.Select(10, 1, 1, 50, { 3440, 1440 }).reason == BypassReason::Duplicate,
            "duplicate suppression");

    routes.Release(10);
    routes.Register(12, 1, 1, { 3440, 1440 });
    routes.Observe(12, 1, { 3440, 1440 });
    const auto reselection = routes.Select(12, 1, 1, 51, { 3440, 1440 });
    Require(reselection.execute && reselection.resetHistory, "same-contract reselection resets history");

    routes.Register(13, 1, 3, { 2560, 1440 });
    routes.Observe(13, 3, { 2560, 1440 });
    Require(routes.Select(13, 1, 3, 52, { 2560, 1440 }).reason == BypassReason::ContractChanged,
            "physical contract change bypass");
    Require(routes.Select(12, 1, 1, 53, { 3440, 1440 }).reason == BypassReason::ContractChanged,
            "contract-change latch");
}

int main()
{
    SelectedRouteAndAuxiliaryBypass();
    AmbiguityAndCorrelationFailClosed();
    DuplicateReleaseAndContractChange();
    std::puts("Vulkan RR routing tests passed");
    return 0;
}
