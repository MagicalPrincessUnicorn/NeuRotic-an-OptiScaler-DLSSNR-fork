/*
 * Full Blackwell temporal framework for Ada, Ampere and Turing.
 * SPDX-License-Identifier: MIT
 *
 * The exact DLSS-G 310.9.1 Blackwell motion-vector, inpaint and inpaint-
 * decision PTX programs are rebuilt as sm_89, sm_86 or sm_75 fatbins. Intermediate Scatter
 * Retention is an optional rewrite of the motion-vector program inside this
 * complete temporal backend; it is never applied as a standalone redirect.
 */

#pragma once

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "./validated_warp.hpp"

namespace mfgunlock::blackwelltemporal {

using Redirect = validatedwarp::Redirect;

enum class KernelRole {
  kMotionVector,
  kInpaint,
  kInpaintDecision,
};

enum class BoundaryArtifactMode : unsigned int {
  kOff = 0,
  kBalanced = 1,
  kAggressive = 2,
};

inline BoundaryArtifactMode ParseBoundaryArtifactMode(int value) {
  switch (value) {
    case static_cast<int>(BoundaryArtifactMode::kBalanced):
      return BoundaryArtifactMode::kBalanced;
    case static_cast<int>(BoundaryArtifactMode::kAggressive):
      return BoundaryArtifactMode::kAggressive;
    default:
      return BoundaryArtifactMode::kOff;
  }
}

inline const char* BoundaryArtifactModeName(BoundaryArtifactMode mode) {
  switch (mode) {
    case BoundaryArtifactMode::kBalanced: return "Balanced";
    case BoundaryArtifactMode::kAggressive: return "Aggressive";
    default: return "Off";
  }
}

inline const char* RoleName(KernelRole role) {
  switch (role) {
    case KernelRole::kMotionVector: return "motion-vector";
    case KernelRole::kInpaint: return "inpaint";
    case KernelRole::kInpaintDecision: return "inpaint-decision";
    default: return "unknown";
  }
}

struct PreparedKernel {
  KernelRole role = KernelRole::kMotionVector;
  validatedwarp::internal::PreparedRedirect redirect;
};

struct Plan {
  std::array<PreparedKernel, 3> kernels{};
  bool intermediate_scatter = false;
  BoundaryArtifactMode boundary_mode = BoundaryArtifactMode::kOff;
  bool ready = false;
  uint32_t target_sm = 86;
};

struct Result {
  bool detected = false;
  bool applied = false;
  bool intermediate_scatter = false;
  BoundaryArtifactMode boundary_mode = BoundaryArtifactMode::kOff;
  bool motion_vector = false;
  bool inpaint = false;
  bool inpaint_decision = false;
  bool fallback_safe = true;
  std::string detail;
};

namespace internal {

inline constexpr auto& kMotionVectorProfile = profiles::kMotionVectorPtx;

inline constexpr auto& kInpaintProfile = profiles::kInpaintPtx;

inline constexpr auto& kInpaintDecisionProfile = profiles::kInpaintDecisionPtx;

struct KernelSpec {
  KernelRole role;
  const validatedwarp::internal::PtxProfile* profile;
  size_t ada_cubin_slot_size;
  uint64_t ada_cubin_fnv1a64;
};

constexpr std::array<KernelSpec, 3> kKernelSpecs = {{
    {KernelRole::kMotionVector, &kMotionVectorProfile, profiles::kQualityKernels[0].ada_cubin_bytes,
     profiles::kQualityKernels[0].ada_cubin_hash},
    {KernelRole::kInpaint, &kInpaintProfile, profiles::kQualityKernels[1].ada_cubin_bytes,
     profiles::kQualityKernels[1].ada_cubin_hash},
    {KernelRole::kInpaintDecision, &kInpaintDecisionProfile, profiles::kQualityKernels[2].ada_cubin_bytes,
     profiles::kQualityKernels[2].ada_cubin_hash},
}};

constexpr char kMotionDivisorLoad[] =
    "ld.param.f32 %f2, [Kernel_EstimateIntermMvecsScatter_param_0+120];\n";
constexpr char kMotionRegisterAnchor[] = ".reg .pred %p<656>;\n";
constexpr char kBoundaryBalancedMarker[] = "MFGUNLOCK_BOUNDARY_ARTIFACT_BALANCED_V1";
constexpr char kBoundaryAggressiveMarker[] = "MFGUNLOCK_BOUNDARY_ARTIFACT_AGGRESSIVE_V1";

struct BoundaryDirection {
  const char* marker;
  const char* anchor;
  const char* center_x;
  const char* center_y;
  const char* center_depth;
  std::array<std::array<const char*, 3>, 4> neighbors;
  const char* motion_length;
  bool reload_divisor;
};

// Exact 310.9.1 shared-tile mapping. Profile hashes and unique anchors guard
// every use before this register mapping can reach a rebuilt provider fatbin.
constexpr std::array<BoundaryDirection, 2> kBoundaryDirections = {{
    {"CURR_TO_PREV", "fma.rn.ftz.f32 %f14, %f7, %f7, %f157;\n",
     "%f7", "%f8", "%f9",
     {{{"%f55", "%f56", "%f57"}, {"%f78", "%f79", "%f80"},
       {"%f97", "%f98", "%f99"}, {"%f120", "%f121", "%f122"}}},
     "%f14", false},
    {"PREV_TO_CURR", "fma.rn.ftz.f32 %f22, %f15, %f15, %f942;\n",
     "%f15", "%f16", "%f17",
     {{{"%f840", "%f841", "%f842"}, {"%f863", "%f864", "%f865"},
       {"%f882", "%f883", "%f884"}, {"%f905", "%f906", "%f907"}}},
     "%f22", true},
}};

inline bool MotionVectorAlreadyModified(const std::string& ptx) {
  return ptx.find("MFGUNLOCK_INTERMEDIATE_SCATTER_V1") != std::string::npos ||
      ptx.find(kBoundaryBalancedMarker) != std::string::npos ||
      ptx.find(kBoundaryAggressiveMarker) != std::string::npos;
}

inline void AppendBoundaryNeighbor(std::ostringstream& stream,
                                   const BoundaryDirection& direction,
                                   const std::array<const char*, 3>& neighbor,
                                   const char* depth_limit, bool aggressive) {
  stream << "sub.ftz.f32 %qgf3, " << neighbor[0] << ", " << direction.center_x << ";\n"
         << "sub.ftz.f32 %qgf4, " << neighbor[1] << ", " << direction.center_y << ";\n"
         << "mul.ftz.f32 %qgf5, %qgf4, %qgf4;\n"
         << "fma.rn.ftz.f32 %qgf5, %qgf3, %qgf3, %qgf5;\n"
         << "div.approx.ftz.f32 %qgf6, %qgf5, %qgf2;\n"
         << "sub.ftz.f32 %qgf6, 0f3F800000, %qgf6;\n"
         << "setp.gt.f32 %qgp0, %qgf6, 0f00000000;\n"
         << "sub.ftz.f32 %qgf7, " << neighbor[2] << ", " << direction.center_depth << ";\n"
         << "abs.ftz.f32 %qgf7, %qgf7;\n"
         << "setp.lt.and.f32 %qgp0, %qgf7, " << depth_limit << ", %qgp0;\n"
         << "@!%qgp0 mov.f32 %qgf6, 0f00000000;\n";
  if (aggressive) {
    stream << "add.f32 %qgf0, %qgf0, %qgf6;\n";
  } else {
    stream << "min.ftz.f32 %qgf6, %qgf6, 0f3F800000;\n"
           << "max.f32 %qgf0, %qgf0, %qgf6;\n";
  }
}

inline void AppendBoundaryScale(std::ostringstream& stream, bool aggressive) {
  if (aggressive) {
    stream << "sub.f32 %qgf0, %qgf0, 0f3F800000;\n"
           << "max.f32 %qgf0, %qgf0, 0f00000000;\n"
           << "min.f32 %qgf0, %qgf0, 0f3F800000;\n"
           << "mul.f32 %qgf0, %qgf0, %qgf0;\n"
           << "fma.rn.f32 %qgf11, %qgf0, 0fBE800000, 0f3F800000;\n";
  } else {
    stream << "fma.rn.f32 %qgf11, %qgf0, 0fBF000000, 0f3F800000;\n";
  }
  stream << "mul.ftz.f32 %f2, %f2, %qgf11;\n";
}

inline std::string BuildBoundaryProgram(const BoundaryDirection& direction,
                                        BoundaryArtifactMode mode) {
  const bool aggressive = mode == BoundaryArtifactMode::kAggressive;
  const char* depth_limit = aggressive ? "0f40000000" : "0f40400000";
  std::ostringstream stream;
  stream << "// " << (aggressive ? kBoundaryAggressiveMarker : kBoundaryBalancedMarker)
         << '_' << direction.marker << '\n';
  if (direction.reload_divisor) stream << kMotionDivisorLoad;
  stream << "mov.f32 %qgf0, 0f00000000;\n"
         << "div.approx.ftz.f32 %qgf2, " << direction.motion_length << ", %f2;\n"
         << "max.ftz.f32 %qgf2, %qgf2, 0f3F800000;\n";
  for (const auto& neighbor : direction.neighbors)
    AppendBoundaryNeighbor(stream, direction, neighbor, depth_limit, aggressive);
  AppendBoundaryScale(stream, aggressive);
  return stream.str();
}

inline bool RewriteMotionVector(std::string& ptx, BoundaryArtifactMode mode,
                                std::string& why) {
  if (MotionVectorAlreadyModified(ptx)) {
    why = "motion-vector PTX is already modified";
    return false;
  }

  if (mode == BoundaryArtifactMode::kOff) {
    return validatedwarp::internal::ReplaceOnce(
        ptx, kMotionDivisorLoad,
        std::string(kMotionDivisorLoad) +
            "mul.ftz.f32 %f2, %f2, 0f3F000000; // MFGUNLOCK_INTERMEDIATE_SCATTER_V1\n",
        why);
  }
  if (mode != BoundaryArtifactMode::kBalanced &&
      mode != BoundaryArtifactMode::kAggressive) {
    why = "unknown boundary artifact mode";
    return false;
  }
  if (!validatedwarp::internal::ReplaceOnce(
          ptx, kMotionDivisorLoad, kMotionDivisorLoad, why)) {
    return false;
  }

  if (!validatedwarp::internal::ReplaceOnce(
          ptx, kMotionRegisterAnchor,
          std::string(kMotionRegisterAnchor) +
              ".reg .pred %qgp<2>;\n.reg .f32 %qgf<12>;\n",
          why)) {
    return false;
  }
  for (const auto& direction : kBoundaryDirections) {
    if (!validatedwarp::internal::ReplaceOnce(
            ptx, direction.anchor,
            std::string(direction.anchor) + BuildBoundaryProgram(direction, mode), why)) {
      return false;
    }
  }
  return true;
}

inline bool RewriteMotionVectorOff(std::string& ptx, std::string& why) {
  return RewriteMotionVector(ptx, BoundaryArtifactMode::kOff, why);
}

inline bool RewriteMotionVectorBalanced(std::string& ptx, std::string& why) {
  return RewriteMotionVector(ptx, BoundaryArtifactMode::kBalanced, why);
}

inline bool RewriteMotionVectorAggressive(std::string& ptx, std::string& why) {
  return RewriteMotionVector(ptx, BoundaryArtifactMode::kAggressive, why);
}

inline validatedwarp::internal::RewritePtxCallback MotionVectorRewrite(
    BoundaryArtifactMode mode) {
  switch (mode) {
    case BoundaryArtifactMode::kOff: return RewriteMotionVectorOff;
    case BoundaryArtifactMode::kBalanced: return RewriteMotionVectorBalanced;
    case BoundaryArtifactMode::kAggressive: return RewriteMotionVectorAggressive;
    default: return nullptr;
  }
}

inline bool ValidateAdaCubinSlot(
    const validatedwarp::internal::PreparedRedirect& prepared,
    const KernelSpec& spec, std::string& why) {
  std::vector<fatbin::Entry> entries;
  size_t end = 0;
  if (!fatbin::Parse(std::span<const unsigned char>(
          prepared.located.address, prepared.located.size), entries, end) ||
      end != prepared.located.size || entries.size() != 3) {
    why = "temporal fatbin layout changed";
    return false;
  }
  const auto& cubin = entries[2];
  if (cubin.kind != 2 || cubin.architecture != validatedwarp::internal::kAdaArch ||
      cubin.payload_bytes != spec.ada_cubin_slot_size) {
    why = "original Ada cubin slot layout changed";
    return false;
  }
  const auto* payload = prepared.located.address + cubin.PayloadOffset();
  if (validatedwarp::internal::Fnv1a64(payload, cubin.payload_bytes) !=
      spec.ada_cubin_fnv1a64) {
    why = "original Ada cubin slot fingerprint changed";
    return false;
  }
  return true;
}

inline bool ValidateDisjointPlan(const Plan& plan, std::string& why) {
  for (size_t left = 0; left < plan.kernels.size(); ++left) {
    const auto& lhs = plan.kernels[left].redirect;
    if (lhs.located.address == nullptr || lhs.slots.empty()) {
      why = "prepared temporal kernel is incomplete";
      return false;
    }
    for (size_t right = left + 1; right < plan.kernels.size(); ++right) {
      const auto& rhs = plan.kernels[right].redirect;
      if (lhs.located.address == rhs.located.address) {
        why = "two temporal roles resolved to the same provider fatbin";
        return false;
      }
      for (uint64_t* lhs_slot : lhs.slots) {
        if (std::find(rhs.slots.begin(), rhs.slots.end(), lhs_slot) != rhs.slots.end()) {
          why = "two temporal roles share a provider descriptor slot";
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace internal

inline bool Prepare(HMODULE module, bool enable_intermediate_scatter,
                    BoundaryArtifactMode boundary_mode,
                    Plan& plan, Result& result, std::string& provider_version,
                    uint32_t target_sm = 86) {
  plan = {};
  result = {};
  if (target_sm != 75 && target_sm != 86 && target_sm != 89) {
    result.detail = "unsupported temporal target";
    return false;
  }
  plan.target_sm = target_sm;
  if (!validatedwarp::IsSupportedProvider(module, provider_version, result.detail)) return false;

  const auto motion_rewrite = enable_intermediate_scatter
      ? internal::MotionVectorRewrite(boundary_mode)
      : nullptr;
  if (enable_intermediate_scatter && motion_rewrite == nullptr) {
    result.detail = "unknown boundary artifact mode";
    return false;
  }

  for (size_t index = 0; index < internal::kKernelSpecs.size(); ++index) {
    const auto& spec = internal::kKernelSpecs[index];
    plan.kernels[index].role = spec.role;
    const auto rewrite = spec.role == KernelRole::kMotionVector ? motion_rewrite : nullptr;
    std::string detail;
    if (!validatedwarp::internal::PrepareRetargetedRedirect(
            module, *spec.profile, rewrite, plan.kernels[index].redirect, detail, target_sm) ||
        !internal::ValidateAdaCubinSlot(
            plan.kernels[index].redirect, spec, detail)) {
      std::ostringstream stream;
      stream << RoleName(spec.role) << " prepare failed: " << detail;
      result.detail = stream.str();
      return false;
    }
  }

  if (!internal::ValidateDisjointPlan(plan, result.detail)) return false;
  plan.intermediate_scatter = enable_intermediate_scatter;
  plan.boundary_mode = enable_intermediate_scatter
      ? boundary_mode
      : BoundaryArtifactMode::kOff;
  plan.ready = true;
  result.detected = true;
  return true;
}

inline bool Commit(Plan& plan, std::vector<Redirect>& redirects, Result& result) {
  if (!plan.ready) {
    result.detail = "Blackwell temporal redirect plan is not ready";
    return false;
  }

  std::vector<Redirect> committed;
  committed.reserve(plan.kernels.size());
  redirects.reserve(redirects.size() + plan.kernels.size());
  std::ostringstream details;
  for (size_t index = 0; index < plan.kernels.size(); ++index) {
    auto& kernel = plan.kernels[index];
    Redirect redirect;
    std::string detail;
    const std::string label = "path=Blackwell temporal sm_" +
        std::to_string(plan.target_sm) + "; role=" + RoleName(kernel.role);
    if (!validatedwarp::internal::CommitPreparedRedirect(
            kernel.redirect, label, redirect, detail)) {
      if (redirect.allocation != nullptr) committed.push_back(std::move(redirect));
      result.fallback_safe = validatedwarp::Restore(committed);
      for (auto& pending : committed) redirects.push_back(std::move(pending));
      std::ostringstream stream;
      stream << RoleName(kernel.role) << " commit failed: " << detail;
      if (result.fallback_safe)
        stream << "; previously committed temporal redirects were rolled back";
      else
        stream << "; incomplete rollback; temporal fallback blocked";
      result.detail = stream.str();
      plan.ready = false;
      return false;
    }
    if (index != 0) details << " | ";
    details << detail;
    committed.push_back(std::move(redirect));
  }

  for (auto& redirect : committed) redirects.push_back(std::move(redirect));
  plan.ready = false;
  result.applied = true;
  result.intermediate_scatter = plan.intermediate_scatter;
  result.boundary_mode = plan.boundary_mode;
  result.motion_vector = true;
  result.inpaint = true;
  result.inpaint_decision = true;
  result.detail = details.str();
  return true;
}

inline bool Restore(std::vector<Redirect>& redirects) {
  return validatedwarp::Restore(redirects);
}

}  // namespace mfgunlock::blackwelltemporal
