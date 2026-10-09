/* SPDX-License-Identifier: MIT */
#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mfgunlock::ptx {

inline constexpr std::string_view kPackedHalfMarker = "MFGUNLOCK_SM75_PACKED_HALF";

inline bool HasPackedHalfConversion(std::string_view text) {
  return text.find(".f16x2.f32") != text.npos;
}

inline uint64_t SourceFingerprint(std::string_view text) {
  uint64_t value = 0xcbf29ce484222325ull;
  for (unsigned char byte : text) value = (value ^ byte) * 0x100000001b3ull;
  return value;
}

namespace internal {

inline bool Consume(std::string_view& text, std::string_view prefix) {
  if (!text.starts_with(prefix)) return false;
  text.remove_prefix(prefix.size());
  return true;
}

inline bool ReadNumber(std::string_view& text, unsigned int& number) {
  if (text.empty() || text.front() < '0' || text.front() > '9') return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
  if (result.ec != std::errc{}) return false;
  text.remove_prefix(static_cast<size_t>(result.ptr - text.data()));
  return true;
}

inline bool RegisterLimit(std::string_view text, std::string_view prefix,
                          unsigned int& limit, size_t& end) {
  const auto start = text.find(prefix);
  if (start == text.npos || text.find(prefix, start + prefix.size()) != text.npos) return false;
  auto rest = text.substr(start + prefix.size());
  if (!ReadNumber(rest, limit) || limit == 0 || !Consume(rest, ">;\n")) return false;
  end = static_cast<size_t>(rest.data() - text.data());
  return true;
}

inline bool ReadRegister(std::string_view& text, std::string_view prefix,
                         unsigned int limit, std::string_view& operand) {
  const auto original = text;
  unsigned int number = 0;
  if (!Consume(text, prefix) || !ReadNumber(text, number) || number >= limit) return false;
  operand = original.substr(0, original.size() - text.size());
  return true;
}

}  // namespace internal

// Call only after exact source qualification. The accepted form is the complete
// unpredicated brace statement in the inspected 310.9.1 PTX, not a fuzzy match.
inline bool LowerPackedHalf(std::string& text, uint32_t target_sm,
                            size_t expected_count, std::string& why) {
  why.clear();
  if (target_sm == 86 || target_sm == 89) return true;
  const auto reject = [&](const char* reason) { why = reason; return false; };
  if (target_sm != 75) return reject("unsupported PTX compatibility target");
  if (text.find(kPackedHalfMarker) != text.npos || text.find("%mfg_h") != text.npos)
    return reject("packed-half lowering already present or register collision");
  if (expected_count == 0)
    return !HasPackedHalfConversion(text) || reject("unexpected packed-half conversion");

  unsigned int integer_limit = 0;
  unsigned int float_limit = 0;
  size_t declarations_end = 0;
  size_t ignored_end = 0;
  if (!internal::RegisterLimit(text, ".reg .b32 %r<", integer_limit, declarations_end) ||
      !internal::RegisterLimit(text, ".reg .f32 %f<", float_limit, ignored_end))
    return reject("packed-half register declarations changed");

  std::string output;
  size_t count = 0;
  for (size_t offset = 0; offset < text.size();) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == text.npos ? text.size() : newline + 1;
    const auto line = std::string_view(text).substr(offset, end - offset);
    if (!HasPackedHalfConversion(line)) {
      output.append(line);
    } else {
      auto rest = line;
      std::string_view destination, high, low;
      if (offset < ignored_end || offset < declarations_end ||
          !internal::Consume(rest, "{ cvt.rn.f16x2.f32 ") ||
          !internal::ReadRegister(rest, "%r", integer_limit, destination) ||
          !internal::Consume(rest, ", ") ||
          !internal::ReadRegister(rest, "%f", float_limit, high) ||
          !internal::Consume(rest, ", ") ||
          !internal::ReadRegister(rest, "%f", float_limit, low) ||
          !internal::Consume(rest, "; }") || (rest != "\n" && !rest.empty()))
        return reject("unrecognized packed-half conversion form");
      // PTX cvt.f16x2 places source a in bits 31:16 and b in bits 15:0.
      // mov.b32 packing is low-first. Scalar RN keeps subnormals (no .ftz).
      output += "{\ncvt.rn.f16.f32 %mfg_h0, ";
      output.append(high);
      output += ";\ncvt.rn.f16.f32 %mfg_h1, ";
      output.append(low);
      output += ";\nmov.b32 ";
      output.append(destination);
      output += ", {%mfg_h1, %mfg_h0};\n}";
      output.append(rest);
      ++count;
    }
    offset = end;
  }
  if (count != expected_count) return reject("packed-half conversion count changed");
  output.insert(declarations_end,
                ".reg .b16 %mfg_h<2>; // " + std::string(kPackedHalfMarker) + "\n");
  text.swap(output);
  return true;
}


inline constexpr std::string_view kTuringMma16816 =
    "mma.sync.aligned.m16n8k16.row.col.f16.f16.f16.f16";
inline constexpr std::string_view kTuringMma1688 =
    "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16";

inline bool HasTuringMma16816(std::string_view text) {
  return text.find(kTuringMma16816) != text.npos;
}

namespace internal {

template <size_t N>
inline bool ReadRegisterGroup(std::string_view& text, unsigned int limit,
                              std::array<std::string_view, N>& registers) {
  if (!Consume(text, "{")) return false;
  for (size_t index = 0; index < N; ++index) {
    if (!ReadRegister(text, "%r", limit, registers[index])) return false;
    if (index + 1 != N && !Consume(text, ",")) return false;
  }
  return Consume(text, "}");
}

inline bool SameRegister(std::string_view left, std::string_view right) {
  return left == right;
}

}  // namespace internal

// The inspected 310.9.1 providers use only the f16 accumulator form. Splitting
// K=16 into K=8 + K=8 preserves the PTX fragment mapping: the second half of
// A/B feeds the second MMA and its accumulator is the first MMA result.
inline bool LowerTuringMma16816(std::string& text, uint32_t target_sm,
                                size_t expected_count, std::string& why) {
  why.clear();
  if (target_sm == 86 || target_sm == 89) return true;
  const auto reject = [&](const char* reason) { why = reason; return false; };
  if (target_sm != 75) return reject("unsupported PTX compatibility target");
  if (expected_count == 0)
    return !HasTuringMma16816(text) || reject("unexpected m16n8k16 instruction");

  unsigned int integer_limit = 0;
  size_t declarations_end = 0;
  if (!internal::RegisterLimit(text, ".reg .b32 %r<", integer_limit, declarations_end))
    return reject("MMA register declaration changed");

  std::string output;
  output.reserve(text.size() + expected_count * 96);
  size_t count = 0;
  for (size_t offset = 0; offset < text.size();) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == text.npos ? text.size() : newline + 1;
    const auto line = std::string_view(text).substr(offset, end - offset);
    if (line.find(kTuringMma16816) == line.npos) {
      output.append(line);
      offset = end;
      continue;
    }

    auto rest = line;
    std::array<std::string_view, 2> destination{};
    std::array<std::string_view, 4> a{};
    std::array<std::string_view, 2> b{};
    std::array<std::string_view, 2> accumulator{};
    const std::string prefix = std::string(kTuringMma16816) + " ";
    if (offset < declarations_end || !internal::Consume(rest, prefix) ||
        !internal::ReadRegisterGroup(rest, integer_limit, destination) ||
        !internal::Consume(rest, ", ") ||
        !internal::ReadRegisterGroup(rest, integer_limit, a) ||
        !internal::Consume(rest, ", ") ||
        !internal::ReadRegisterGroup(rest, integer_limit, b) ||
        !internal::Consume(rest, ", ") ||
        !internal::ReadRegisterGroup(rest, integer_limit, accumulator) ||
        !internal::Consume(rest, ";") || (rest != "\n" && !rest.empty())) {
      return reject("unrecognized m16n8k16 instruction form");
    }

    // D is written by the first MMA, so the second K-half must not source a
    // register from D before consuming A2/A3/B1.
    for (const auto out : destination) {
      if (internal::SameRegister(out, a[2]) || internal::SameRegister(out, a[3]) ||
          internal::SameRegister(out, b[1]))
        return reject("m16n8k16 split has an output/input register hazard");
    }

    const auto append_group = [&](const auto& registers) {
      output += '{';
      for (size_t index = 0; index < registers.size(); ++index) {
        if (index != 0) output += ',';
        output.append(registers[index]);
      }
      output += '}';
    };
    output.append(kTuringMma1688);
    output += ' ';
    append_group(destination);
    output += ',';
    append_group(std::array<std::string_view, 2>{a[0], a[1]});
    output += ',';
    append_group(std::array<std::string_view, 1>{b[0]});
    output += ',';
    append_group(accumulator);
    output += ";\n";
    output.append(kTuringMma1688);
    output += ' ';
    append_group(destination);
    output += ',';
    append_group(std::array<std::string_view, 2>{a[2], a[3]});
    output += ',';
    append_group(std::array<std::string_view, 1>{b[1]});
    output += ',';
    append_group(destination);
    output += ';';
    output.append(rest);
    ++count;
    offset = end;
  }

  if (count != expected_count) return reject("m16n8k16 instruction count changed");
  text.swap(output);
  return true;
}


struct HalfMinMaxCounts {
  size_t scalar_max = 0;
  size_t scalar_min = 0;
  size_t packed_max = 0;
  size_t packed_min = 0;

  size_t Total() const {
    return scalar_max + scalar_min + packed_max + packed_min;
  }
};

inline bool HasTuringHalfMinMax(std::string_view text) {
  for (size_t offset = 0; offset < text.size();) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == text.npos ? text.size() : newline + 1;
    const auto line = text.substr(offset, end - offset);
    if ((line.find("max.") != line.npos || line.find("min.") != line.npos) &&
        line.find(".f16") != line.npos) return true;
    offset = end;
  }
  return false;
}

// sm_75 has f16 comparisons/conversions but not f16/f16x2 min/max. The exact
// 310.9.1 forms have no modifiers. Widening f16 to f32 is exact; f32 min/max
// has the same default NaN and signed-zero ordering, then the selected f16
// value is converted back without changing a finite operand.
inline bool LowerTuringHalfMinMax(std::string& text, uint32_t target_sm,
                                  HalfMinMaxCounts expected, std::string& why) {
  why.clear();
  if (target_sm == 86 || target_sm == 89) return true;
  const auto reject = [&](const char* reason) { why = reason; return false; };
  if (target_sm != 75) return reject("unsupported PTX compatibility target");
  if (text.find("%h") != text.npos) return reject("half min/max temporary register collision");
  if (expected.Total() == 0)
    return !HasTuringHalfMinMax(text) || reject("unexpected f16 min/max instruction");

  unsigned int integer_limit = 0;
  unsigned int scalar_limit = 0;
  unsigned int float_limit = 0;
  size_t integer_end = 0;
  size_t scalar_end = 0;
  size_t float_end = 0;
  if (!internal::RegisterLimit(text, ".reg .b32 %r<", integer_limit, integer_end) ||
      !internal::RegisterLimit(text, ".reg .f32 %f<", float_limit, float_end) ||
      float_limit > UINT32_MAX - 2) {
    return reject("half min/max register declarations changed");
  }
  if (expected.scalar_max + expected.scalar_min != 0 &&
      !internal::RegisterLimit(text, ".reg .b16 %rs<", scalar_limit, scalar_end)) {
    return reject("scalar f16 register declaration changed");
  }

  const std::string f0 = "%f" + std::to_string(float_limit);
  const std::string f1 = "%f" + std::to_string(float_limit + 1);
  std::string output;
  output.reserve(text.size() + expected.Total() * 96);
  HalfMinMaxCounts actual{};

  const auto emit_scalar = [&](std::string_view op, std::string_view destination,
                               std::string_view a, std::string_view b) {
    output += "{\ncvt.f32.f16 ";
    output += f0;
    output += ',';
    output.append(a);
    output += ";\ncvt.f32.f16 ";
    output += f1;
    output += ',';
    output.append(b);
    output += ";\n";
    output.append(op);
    output += ".f32 ";
    output += f0 + ',' + f0 + ',' + f1;
    output += ";\ncvt.rn.f16.f32 ";
    output.append(destination);
    output += ',';
    output += f0;
    output += ";\n";
  };
  const auto emit_packed = [&](std::string_view op, std::string_view destination,
                               std::string_view a, std::string_view b) {
    output += "{\nmov.b32 {%h0,%h1},";
    output.append(a);
    output += ";\nmov.b32 {%h2,%h3},";
    output.append(b);
    output += ";\ncvt.f32.f16 " + f0 + ",%h0;\ncvt.f32.f16 " + f1 + ",%h2;\n";
    output.append(op);
    output += ".f32 " + f0 + ',' + f0 + ',' + f1 + ";\ncvt.rn.f16.f32 %h0," + f0 + ";\n";
    output += "cvt.f32.f16 " + f0 + ",%h1;\ncvt.f32.f16 " + f1 + ",%h3;\n";
    output.append(op);
    output += ".f32 " + f0 + ',' + f0 + ',' + f1 + ";\ncvt.rn.f16.f32 %h1," + f0 + ";\n";
    output += "mov.b32 ";
    output.append(destination);
    output += ",{%h0,%h1};\n";
  };

  for (size_t offset = 0; offset < text.size();) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == text.npos ? text.size() : newline + 1;
    const auto line = std::string_view(text).substr(offset, end - offset);
    const bool candidate = (line.find("max.f16") != line.npos ||
                            line.find("min.f16") != line.npos);
    if (!candidate) {
      output.append(line);
      offset = end;
      continue;
    }

    auto parse_scalar = [&](std::string_view prefix, size_t& counter,
                            std::string_view op) {
      auto rest = line;
      std::string_view destination, a, b;
      if (offset < scalar_end || !internal::Consume(rest, prefix) ||
          !internal::ReadRegister(rest, "%rs", scalar_limit, destination) ||
          !internal::Consume(rest, ",") ||
          !internal::ReadRegister(rest, "%rs", scalar_limit, a) ||
          !internal::Consume(rest, ",") ||
          !internal::ReadRegister(rest, "%rs", scalar_limit, b) ||
          !internal::Consume(rest, ";") || (rest != "\n" && !rest.empty())) return false;
      emit_scalar(op, destination, a, b);
      ++counter;
      return true;
    };
    auto parse_packed = [&](std::string_view prefix, size_t& counter,
                            std::string_view op) {
      auto rest = line;
      std::string_view destination, a, b;
      if (offset < integer_end || !internal::Consume(rest, prefix) ||
          !internal::ReadRegister(rest, "%r", integer_limit, destination) ||
          !internal::Consume(rest, ",") ||
          !internal::ReadRegister(rest, "%r", integer_limit, a) ||
          !internal::Consume(rest, ",") ||
          !internal::ReadRegister(rest, "%r", integer_limit, b) ||
          !internal::Consume(rest, ";") || (rest != "\n" && !rest.empty())) return false;
      emit_packed(op, destination, a, b);
      ++counter;
      return true;
    };

    if (!parse_scalar("{max.f16 ", actual.scalar_max, "max") &&
        !parse_scalar("{min.f16 ", actual.scalar_min, "min") &&
        !parse_packed("{max.f16x2 ", actual.packed_max, "max") &&
        !parse_packed("{min.f16x2 ", actual.packed_min, "min")) {
      return reject("unrecognized f16 min/max instruction form");
    }
    offset = end;
  }

  if (actual.scalar_max != expected.scalar_max || actual.scalar_min != expected.scalar_min ||
      actual.packed_max != expected.packed_max || actual.packed_min != expected.packed_min) {
    return reject("f16 min/max instruction count changed");
  }

  const std::string old_float = ".reg .f32 %f<" + std::to_string(float_limit) + ">;\n";
  const std::string new_float = ".reg .f32 %f<" + std::to_string(float_limit + 2) + ">;\n";
  const auto float_at = output.find(old_float);
  if (float_at == output.npos || output.find(old_float, float_at + 1) != output.npos)
    return reject("half min/max f32 declaration changed during rewrite");
  output.replace(float_at, old_float.size(), new_float);

  if (expected.packed_max + expected.packed_min != 0) {
    const std::string integer = ".reg .b32 %r<" + std::to_string(integer_limit) + ">;\n";
    const auto integer_at = output.find(integer);
    if (integer_at == output.npos || output.find(integer, integer_at + 1) != output.npos)
      return reject("half min/max b32 declaration changed during rewrite");
    output.insert(integer_at + integer.size(), ".reg .b16 %h<4>;\n");
  }

  text.swap(output);
  return true;
}

// The exact MMA sources contain large runs of blank/comment-only lines. This
// normalization changes whitespace only and is used solely to keep a rebuilt
// single-image PTX block inside its original provider container.
inline bool CompactProviderPtx(std::string& text, std::string& why) {
  why.clear();
  if (text.find("/*") != text.npos || text.find("*/") != text.npos) {
    why = "block-comment PTX is not eligible for compaction";
    return false;
  }

  std::string output;
  output.reserve(text.size());
  bool first = true;
  for (size_t offset = 0; offset <= text.size();) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == text.npos ? text.size() : newline;
    auto line = std::string_view(text).substr(offset, end - offset);
    const auto first_nonspace = line.find_first_not_of(" \t\r");
    const auto last_nonspace = line.find_last_not_of(" \t\r");
    const auto trimmed = first_nonspace == line.npos
        ? std::string_view{}
        : line.substr(first_nonspace, last_nonspace - first_nonspace + 1);
    if (!trimmed.empty() && trimmed != "//") {
      std::string compact(line);
      if (compact.find('"') == compact.npos && compact.find("//") == compact.npos) {
        for (size_t at = 0; (at = compact.find(", ", at)) != compact.npos;) {
          compact.erase(at + 1, 1);
          ++at;
        }
      }
      if (!first) output += '\n';
      output += compact;
      first = false;
    }
    if (newline == text.npos) break;
    offset = newline + 1;
  }
  if (output.empty()) {
    why = "PTX compaction produced an empty program";
    return false;
  }
  text.swap(output);
  return true;
}

}  // namespace mfgunlock::ptx
