// SPDX-License-Identifier: MIT
// Exercises the exact full-temporal profiles and motion-vector quality rewrites
// without embedding or redistributing NVIDIA kernel payloads.

#include "../../../src/addons/mfgunlock/blackwell_temporal.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace temporal = mfgunlock::blackwelltemporal;
namespace internal = mfgunlock::blackwelltemporal::internal;

namespace {

unsigned int g_checks = 0;

void Check(bool condition, const char* reason) {
  ++g_checks;
  if (!condition) throw std::runtime_error(reason);
}

size_t CountOccurrences(const std::string& text, const std::string& needle) {
  size_t count = 0;
  size_t offset = 0;
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

std::string MotionFixture() {
  return ".version 8.7\n"
         ".target sm_120\n"
         ".reg .pred %p<656>;\n"
         ".visible .entry Kernel_EstimateIntermMvecsScatter() {\n"
         "ld.param.f32 %f2, [Kernel_EstimateIntermMvecsScatter_param_0+120];\n"
         "fma.rn.ftz.f32 %f14, %f7, %f7, %f157;\n"
         "fma.rn.ftz.f32 %f22, %f15, %f15, %f942;\n"
         "ret;\n"
         "}\n";
}

struct NeighborEvidence {
  float dx;
  float dy;
  float depth_delta;
};

float BoundaryScale(temporal::BoundaryArtifactMode mode,
                    const std::array<NeighborEvidence, 4>& neighbors,
                    float native_threshold = 1.0f) {
  const float threshold = std::max(native_threshold, 1.0f);
  const float depth_limit = mode == temporal::BoundaryArtifactMode::kAggressive
      ? 2.0f
      : 3.0f;
  float strongest = 0.0f;
  float sum = 0.0f;
  for (const auto& neighbor : neighbors) {
    float support = 1.0f -
        (neighbor.dx * neighbor.dx + neighbor.dy * neighbor.dy) / threshold;
    const float depth = std::abs(neighbor.depth_delta);
    if (!std::isfinite(support) || !std::isfinite(depth) ||
        support <= 0.0f || !(depth < depth_limit)) {
      support = 0.0f;
    }
    support = std::min(support, 1.0f);
    strongest = std::max(strongest, support);
    sum += support;
  }
  if (mode == temporal::BoundaryArtifactMode::kAggressive) {
    float confidence = std::clamp(sum - 1.0f, 0.0f, 1.0f);
    confidence *= confidence;
    return 1.0f - 0.25f * confidence;
  }
  return 1.0f - 0.5f * strongest;
}

}  // namespace

int main() {
  using temporal::BoundaryArtifactMode;

  temporal::Plan target_plan;
  Check(target_plan.target_sm == 86, "existing temporal default remains SM86");
  temporal::Result target_result;
  std::string target_version;
  Check(!temporal::Prepare(nullptr, false, BoundaryArtifactMode::kOff,
                           target_plan, target_result, target_version, 70),
        "unknown temporal target rejected before source access");
  Check(!target_plan.ready && !target_result.applied, "failed target is not applied");

  temporal::Plan turing_plan;
  temporal::Result turing_result;
  std::string turing_version;
  Check(!temporal::Prepare(nullptr, false, BoundaryArtifactMode::kOff,
                           turing_plan, turing_result, turing_version, 75),
        "SM75 target proceeds to provider qualification");
  Check(turing_plan.target_sm == 75 && !turing_plan.ready,
        "SM75 target is retained without false readiness");

  temporal::Plan ada_plan;
  temporal::Result ada_result;
  std::string ada_version;
  Check(!temporal::Prepare(nullptr, false, BoundaryArtifactMode::kOff,
                           ada_plan, ada_result, ada_version, 89),
        "SM89 target proceeds to exact provider qualification");
  Check(ada_plan.target_sm == 89 && !ada_plan.ready &&
            ada_result.detail != "unsupported temporal target",
        "native SM89 target is accepted without false readiness");

  Check(internal::kKernelSpecs.size() == 3, "three complete temporal roles");
  Check(internal::kMotionVectorProfile.normalized_size == 90731u,
        "motion-vector PTX size");
  Check(internal::kMotionVectorProfile.raw_fnv1a64 == 0xb1a2811b29625d41ull,
        "motion-vector PTX hash");
  Check(internal::kInpaintProfile.normalized_size == 26439u,
        "inpaint PTX size");
  Check(internal::kInpaintProfile.raw_fnv1a64 == 0x546151924160b69bull,
        "inpaint PTX hash");
  Check(internal::kInpaintDecisionProfile.normalized_size == 23116u,
        "inpaint-decision PTX size");
  Check(internal::kInpaintDecisionProfile.raw_fnv1a64 == 0x9b47635b91b2436bull,
        "inpaint-decision PTX hash");
  Check(internal::kMotionVectorProfile.descriptor_references == 8u,
        "motion-vector descriptor count");
  Check(internal::kInpaintProfile.descriptor_references == 8u,
        "inpaint descriptor count");
  Check(internal::kInpaintDecisionProfile.descriptor_references == 8u,
        "inpaint-decision descriptor count");
  Check(std::string(internal::kMotionVectorProfile.entry_name) ==
            "Kernel_EstimateIntermMvecsScatter",
        "motion-vector entrypoint identity");

  Check(internal::kKernelSpecs[0].ada_cubin_slot_size == 39968u &&
            internal::kKernelSpecs[0].ada_cubin_fnv1a64 == 0x9642092def23b3dfull,
        "motion-vector Ada cubin identity");
  Check(internal::kKernelSpecs[1].ada_cubin_slot_size == 17568u &&
            internal::kKernelSpecs[1].ada_cubin_fnv1a64 == 0x1ba6454ab039f9ddull,
        "inpaint Ada cubin identity");
  Check(internal::kKernelSpecs[2].ada_cubin_slot_size == 15136u &&
            internal::kKernelSpecs[2].ada_cubin_fnv1a64 == 0xc4a5eb4a8694f835ull,
        "inpaint-decision Ada cubin identity");

  Check(temporal::ParseBoundaryArtifactMode(-1) == BoundaryArtifactMode::kOff,
        "negative mode is rejected to Off");
  Check(temporal::ParseBoundaryArtifactMode(0) == BoundaryArtifactMode::kOff,
        "Off mode parsing");
  Check(temporal::ParseBoundaryArtifactMode(1) == BoundaryArtifactMode::kBalanced,
        "Balanced mode parsing");
  Check(temporal::ParseBoundaryArtifactMode(2) == BoundaryArtifactMode::kAggressive,
        "Aggressive mode parsing");
  Check(temporal::ParseBoundaryArtifactMode(3) == BoundaryArtifactMode::kOff,
        "unknown mode is rejected to Off");
  Check(std::string(temporal::BoundaryArtifactModeName(BoundaryArtifactMode::kBalanced)) ==
            "Balanced",
        "Balanced mode name");
  Check(std::string(temporal::BoundaryArtifactModeName(BoundaryArtifactMode::kAggressive)) ==
            "Aggressive",
        "Aggressive mode name");
  Check(internal::MotionVectorRewrite(static_cast<BoundaryArtifactMode>(99)) == nullptr,
        "unknown mode has no PTX rewrite callback");

  std::string why;
  std::string off = MotionFixture();
  Check(internal::RewriteMotionVector(off, BoundaryArtifactMode::kOff, why),
        "legacy intermediate scatter rewrite");
  Check(off.find("MFGUNLOCK_INTERMEDIATE_SCATTER_V1") != std::string::npos,
        "legacy ISR marker");
  Check(off.find("0f3F000000") != std::string::npos,
        "legacy ISR retention factor");
  Check(off.find(internal::kBoundaryBalancedMarker) == std::string::npos,
        "Off has no Balanced guard");

  why.clear();
  std::string balanced = MotionFixture();
  Check(internal::RewriteMotionVector(
            balanced, BoundaryArtifactMode::kBalanced, why),
        "Balanced boundary rewrite");
  Check(balanced.find(".reg .pred %qgp<2>;") != std::string::npos,
        "Balanced predicate registers");
  Check(balanced.find(".reg .f32 %qgf<12>;") != std::string::npos,
        "Balanced float registers");
  Check(balanced.find("MFGUNLOCK_INTERMEDIATE_SCATTER_V1") == std::string::npos,
        "Balanced supersedes unconditional ISR");
  Check(balanced.find("0fBF000000") != std::string::npos,
        "Balanced 0.5 relaxation cap");
  Check(balanced.find("0f40400000") != std::string::npos,
        "Balanced depth threshold");
  Check(balanced.find(internal::kBoundaryBalancedMarker) != std::string::npos,
        "Balanced marker");
  Check(balanced.find("MFGUNLOCK_BOUNDARY_ARTIFACT_BALANCED_V1_CURR_TO_PREV") !=
            std::string::npos,
        "Balanced current-to-previous path");
  Check(balanced.find("MFGUNLOCK_BOUNDARY_ARTIFACT_BALANCED_V1_PREV_TO_CURR") !=
            std::string::npos,
        "Balanced previous-to-current path");
  Check(CountOccurrences(balanced, internal::kMotionDivisorLoad) == 2,
        "Balanced reloads the divisor for the reverse direction");
  Check(CountOccurrences(balanced, "setp.lt.and.f32 %qgp0") == 8,
        "Balanced gates four neighbors in both directions");
  Check(CountOccurrences(balanced, "max.f32 %qgf0") == 8,
        "Balanced retains strongest support per direction");

  why.clear();
  std::string aggressive = MotionFixture();
  Check(internal::RewriteMotionVector(
            aggressive, BoundaryArtifactMode::kAggressive, why),
        "Aggressive boundary rewrite");
  Check(aggressive.find("MFGUNLOCK_INTERMEDIATE_SCATTER_V1") == std::string::npos,
        "Aggressive supersedes unconditional ISR");
  Check(aggressive.find("0fBE800000") != std::string::npos,
        "Aggressive 0.25 relaxation cap");
  Check(aggressive.find("0f40000000") != std::string::npos,
        "Aggressive depth threshold");
  Check(aggressive.find("mul.f32 %qgf0, %qgf0, %qgf0;") != std::string::npos,
        "Aggressive nonlinear confidence");
  Check(aggressive.find(internal::kBoundaryAggressiveMarker) != std::string::npos,
        "Aggressive marker");
  Check(CountOccurrences(aggressive, "add.f32 %qgf0, %qgf0, %qgf6;") == 8,
        "Aggressive accumulates four neighbors in both directions");

  why.clear();
  Check(!internal::RewriteMotionVector(off, BoundaryArtifactMode::kOff, why),
        "double legacy rewrite rejected");
  Check(why == "motion-vector PTX is already modified",
        "double legacy rewrite reason");

  why.clear();
  Check(!internal::RewriteMotionVector(
            balanced, BoundaryArtifactMode::kAggressive, why),
        "cross-mode double rewrite rejected");
  Check(why == "motion-vector PTX is already modified",
        "cross-mode rewrite reason");

  why.clear();
  std::string ambiguous_divisor = MotionFixture() + internal::kMotionDivisorLoad;
  Check(!internal::RewriteMotionVector(
            ambiguous_divisor, BoundaryArtifactMode::kBalanced, why),
        "ambiguous divisor anchor rejected");
  Check(why == "required PTX anchor is ambiguous", "ambiguous divisor reason");

  why.clear();
  std::string ambiguous = MotionFixture() + internal::kMotionRegisterAnchor;
  Check(!internal::RewriteMotionVector(
            ambiguous, BoundaryArtifactMode::kBalanced, why),
        "ambiguous register anchor rejected");
  Check(why == "required PTX anchor is ambiguous", "ambiguous anchor reason");

  why.clear();
  std::string missing = MotionFixture();
  const std::string anchor = internal::kBoundaryDirections[0].anchor;
  const auto offset = missing.find(anchor);
  missing.erase(offset, anchor.size());
  Check(!internal::RewriteMotionVector(
            missing, BoundaryArtifactMode::kBalanced, why),
        "missing direction anchor rejected");
  Check(why == "required PTX anchor is missing", "missing anchor reason");

  why.clear();
  std::string invalid = MotionFixture();
  Check(!internal::RewriteMotionVector(
            invalid, static_cast<BoundaryArtifactMode>(99), why),
        "unknown boundary mode rejected");
  Check(why == "unknown boundary artifact mode", "unknown mode reason");

  const std::array<NeighborEvidence, 4> unsupported = {{{20.0f, 20.0f, 0.0f},
      {20.0f, 20.0f, 0.0f}, {20.0f, 20.0f, 0.0f}, {20.0f, 20.0f, 0.0f}}};
  Check(BoundaryScale(BoundaryArtifactMode::kBalanced, unsupported) == 1.0f,
        "unsupported boundary keeps native divisor");

  auto one_neighbor = unsupported;
  one_neighbor[0] = {0.0f, 0.0f, 0.0f};
  Check(BoundaryScale(BoundaryArtifactMode::kBalanced, one_neighbor) == 0.5f,
        "one coherent neighbor reaches full Balanced retention");
  Check(BoundaryScale(BoundaryArtifactMode::kAggressive, one_neighbor) == 1.0f,
        "Aggressive rejects one-neighbor support");

  auto two_neighbors = unsupported;
  two_neighbors[0] = {0.0f, 0.0f, 0.0f};
  two_neighbors[1] = {0.0f, 0.0f, 0.0f};
  Check(BoundaryScale(BoundaryArtifactMode::kAggressive, two_neighbors) == 0.75f,
        "full Aggressive support stops at 0.75 native divisor");

  auto wrong_surface = unsupported;
  wrong_surface[0] = {0.0f, 0.0f, 3.0f};
  Check(BoundaryScale(BoundaryArtifactMode::kBalanced, wrong_surface) == 1.0f,
        "Balanced rejects cross-depth support");
  wrong_surface[0] = {0.0f, 0.0f, 2.0f};
  Check(BoundaryScale(BoundaryArtifactMode::kAggressive, wrong_surface) == 1.0f,
        "Aggressive uses tighter depth boundary");

  auto unordered = unsupported;
  unordered[0] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
  Check(BoundaryScale(BoundaryArtifactMode::kBalanced, unordered) == 1.0f,
        "NaN motion cannot add support");
  unordered[0] = {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
  Check(BoundaryScale(BoundaryArtifactMode::kBalanced, unordered) == 1.0f,
        "NaN depth cannot add support");

  std::mt19937 random(0x5A17u);
  std::uniform_real_distribution<float> motion(-2.0f, 2.0f);
  std::uniform_real_distribution<float> depth(-4.0f, 4.0f);
  for (unsigned int sample = 0; sample < 1000; ++sample) {
    std::array<NeighborEvidence, 4> neighbors{};
    for (auto& neighbor : neighbors) {
      neighbor = {motion(random), motion(random), depth(random)};
    }
    const float balanced_scale = BoundaryScale(BoundaryArtifactMode::kBalanced, neighbors);
    const float aggressive_scale = BoundaryScale(BoundaryArtifactMode::kAggressive, neighbors);
    Check(balanced_scale >= 0.5f && balanced_scale <= 1.0f,
          "Balanced scale remains bounded");
    Check(aggressive_scale >= 0.75f && aggressive_scale <= 1.0f,
          "Aggressive scale remains bounded");
    Check(aggressive_scale >= balanced_scale,
          "Aggressive never becomes more permissive than Balanced");
  }

  Check(std::string(temporal::RoleName(temporal::KernelRole::kMotionVector)) ==
            "motion-vector",
        "motion-vector role name");
  Check(std::string(temporal::RoleName(temporal::KernelRole::kInpaint)) == "inpaint",
        "inpaint role name");
  Check(std::string(temporal::RoleName(temporal::KernelRole::kInpaintDecision)) ==
            "inpaint-decision",
        "inpaint-decision role name");

  std::cout << "blackwell_temporal_test: " << g_checks << " checks passed\n";
  return 0;
}
