/*
 * Minimal CUDA fatbin parser used by the DLSS-G PTX retargeter.
 * SPDX-License-Identifier: MIT
 *
 * NVIDIA does not publish this container format as a stable ABI. Every parser
 * check below is therefore deliberately conservative: unknown headers, entry
 * layouts, compression flags, or sizes are rejected rather than guessed.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace mfgunlock::fatbin {

inline constexpr uint32_t kMagic = 0xBA55ED50u;
inline constexpr size_t kHeaderBytes = 16;
inline constexpr size_t kMaxPtxBytes = 32 * 1024 * 1024;
inline constexpr size_t kMaxFatbinBytes = 64 * 1024 * 1024;

inline uint16_t ReadU16(const unsigned char* data) {
  uint16_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

inline uint32_t ReadU32(const unsigned char* data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

inline uint64_t ReadU64(const unsigned char* data) {
  uint64_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

// Plain LZ4 block format. The optional mapping identifies the source literal
// responsible for one decoded byte. A second full decode is still required to
// prove that editing that literal does not affect any other output position.
inline bool Lz4BlockDecompress(const unsigned char* src, size_t src_bytes,
                               unsigned char* dst, size_t dst_bytes,
                               size_t wanted = std::numeric_limits<size_t>::max(),
                               size_t* literal = nullptr) {
  size_t in = 0;
  size_t out = 0;

  if (literal != nullptr) *literal = std::numeric_limits<size_t>::max();

  auto extend = [&](size_t& length) {
    if (length != 15) return true;

    unsigned int extra = 0;
    do {
      if (in == src_bytes) return false;
      extra = src[in++];
      if (length > dst_bytes || extra > dst_bytes - length) return false;
      length += extra;
    } while (extra == 255);

    return true;
  };

  while (in < src_bytes) {
    const unsigned int token = src[in++];
    size_t length = token >> 4;
    if (!extend(length) || length > src_bytes - in || length > dst_bytes - out) return false;

    if (literal != nullptr && wanted >= out && wanted - out < length) {
      *literal = in + wanted - out;
    }

    if (length != 0) std::memcpy(dst + out, src + in, length);
    in += length;
    out += length;

    if (in == src_bytes) return out == dst_bytes;
    if (src_bytes - in < 2) return false;

    const size_t distance = ReadU16(src + in);
    in += 2;
    if (distance == 0 || distance > out) return false;

    length = token & 15;
    if (!extend(length) || length > std::numeric_limits<size_t>::max() - 4) return false;
    length += 4;
    if (length > dst_bytes - out) return false;

    // LZ4 match copies may overlap. A byte-at-a-time copy is intentional here;
    // memcpy and memmove do not have the same semantics for this format.
    for (size_t i = 0; i < length; ++i) {
      if (i >= dst_bytes - out) return false;
      const size_t dst_index = out + i;
      if (distance > dst_index) return false;
      dst[dst_index] = dst[dst_index - distance];
    }
    out += length;
  }

  return out == dst_bytes;
}


namespace internal {

inline uint16_t Lz4Hash(const unsigned char* data) {
  return static_cast<uint16_t>((ReadU32(data) * 2654435761u) >> 16);
}

inline void Lz4WriteLength(std::vector<unsigned char>& output, size_t value) {
  while (value >= 255) {
    output.push_back(255);
    value -= 255;
  }
  output.push_back(static_cast<unsigned char>(value));
}

struct Lz4Match {
  size_t length = 0;
  size_t source = 0;
};

}  // namespace internal

// Startup-only compressor for exact provider PTX rebuilds. It intentionally
// trades CPU time for ratio and always round-trips through the decoder before
// a replacement is published. The final five bytes remain literals as required
// by portable LZ4 block decoders.
inline bool Lz4BlockCompress(std::string_view source, size_t max_bytes,
                             std::vector<unsigned char>& output) {
  output.clear();
  if (source.empty() || source.size() > kMaxPtxBytes || max_bytes == 0) return false;

  constexpr int32_t kNone = -1;
  constexpr size_t kHashSize = 1u << 16;
  constexpr size_t kWindow = 65535;
  constexpr size_t kLastLiterals = 5;
  constexpr size_t kLastMatchStart = 12;
  std::array<int32_t, kHashSize> head{};
  head.fill(kNone);
  std::vector<int32_t> chain(source.size(), kNone);
  const auto* data = reinterpret_cast<const unsigned char*>(source.data());

  const auto insert = [&](size_t position) {
    if (position + 4 > source.size()) return;
    const auto hash = internal::Lz4Hash(data + position);
    chain[position] = head[hash];
    head[hash] = static_cast<int32_t>(position);
  };

  const auto best_match = [&](size_t position) {
    internal::Lz4Match best{};
    if (position + 4 > source.size() || position + kLastMatchStart > source.size()) return best;
    const size_t max_length = source.size() - kLastLiterals - position;
    if (max_length < 4) return best;
    const auto hash = internal::Lz4Hash(data + position);
    int32_t candidate = head[hash];
    size_t searched = 0;
    while (candidate != kNone && searched++ < kWindow) {
      const size_t previous = static_cast<size_t>(candidate);
      if (previous >= position || position - previous > kWindow) break;
      if (std::memcmp(data + previous, data + position, 4) == 0) {
        size_t length = 4;
        while (length < max_length && data[previous + length] == data[position + length]) {
          ++length;
        }
        if (length > best.length) {
          best = {length, previous};
          if (length == max_length) break;
        }
      }
      candidate = chain[previous];
    }
    return best;
  };

  const auto exceeds = [&]() { return output.size() > max_bytes; };
  const auto emit = [&](size_t anchor, size_t position, const internal::Lz4Match& match) {
    const size_t literal_length = position - anchor;
    const size_t encoded_match = match.length - 4;
    const size_t token = (std::min<size_t>(literal_length, 15) << 4) |
                         std::min<size_t>(encoded_match, 15);
    output.push_back(static_cast<unsigned char>(token));
    if (literal_length >= 15) internal::Lz4WriteLength(output, literal_length - 15);
    output.insert(output.end(), data + anchor, data + position);
    const size_t distance = position - match.source;
    output.push_back(static_cast<unsigned char>(distance));
    output.push_back(static_cast<unsigned char>(distance >> 8));
    if (encoded_match >= 15) internal::Lz4WriteLength(output, encoded_match - 15);
  };

  size_t anchor_position = 0;
  size_t position = 0;
  while (position + 4 <= source.size()) {
    auto match = best_match(position);
    if (match.length < 4) {
      insert(position++);
      continue;
    }

    // One-byte lazy parsing materially improves the ratio on the exact PTX
    // corpus while keeping the implementation deterministic and bounded.
    while (position + 5 <= source.size()) {
      insert(position);
      const auto next = best_match(position + 1);
      if (next.length < match.length + 1) break;
      ++position;
      match = next;
    }

    emit(anchor_position, position, match);
    if (exceeds()) {
      output.clear();
      return false;
    }
    const size_t match_end = position + match.length;
    for (size_t cursor = position; cursor < match_end; ++cursor) {
      if (cursor + 4 > source.size()) break;
      const auto hash = internal::Lz4Hash(data + cursor);
      if (head[hash] != static_cast<int32_t>(cursor)) insert(cursor);
    }
    position = match_end;
    anchor_position = position;
  }

  const size_t literal_length = source.size() - anchor_position;
  output.push_back(static_cast<unsigned char>(std::min<size_t>(literal_length, 15) << 4));
  if (literal_length >= 15) internal::Lz4WriteLength(output, literal_length - 15);
  output.insert(output.end(), data + anchor_position, data + source.size());
  if (exceeds()) {
    output.clear();
    return false;
  }

  std::vector<unsigned char> decoded(source.size());
  if (!Lz4BlockDecompress(output.data(), output.size(), decoded.data(), decoded.size()) ||
      !std::equal(decoded.begin(), decoded.end(), data)) {
    output.clear();
    return false;
  }
  return true;
}


// Bounded high-ratio fallback for the few exact provider PTX programs whose
// SM75 compatibility lowering no longer fits with the fast parser above. Match
// discovery and lookahead are deliberately capped so provider startup remains
// deterministic. Every output still round-trips through the same decoder.
inline bool Lz4BlockCompressHigh(std::string_view source, size_t max_bytes,
                                 std::vector<unsigned char>& output) {
  output.clear();
  if (source.empty() || source.size() > kMaxPtxBytes || max_bytes == 0) return false;

  constexpr int32_t kNone = -1;
  constexpr size_t kHashSize = 1u << 16;
  constexpr size_t kWindow = 65535;
  constexpr size_t kDepth = 128;
  constexpr size_t kLookahead = 128;
  constexpr size_t kLastLiterals = 5;
  constexpr size_t kLastMatchStart = 12;
  constexpr size_t kInfinity = std::numeric_limits<size_t>::max() / 4;

  const auto* data = reinterpret_cast<const unsigned char*>(source.data());
  std::array<int32_t, kHashSize> head{};
  head.fill(kNone);
  std::vector<int32_t> chain(source.size(), kNone);
  std::vector<internal::Lz4Match> matches(source.size());

  for (size_t position = 0; position + 4 <= source.size(); ++position) {
    const auto hash = internal::Lz4Hash(data + position);
    int32_t candidate = head[hash];
    internal::Lz4Match best{};
    if (position + kLastMatchStart <= source.size()) {
      const size_t max_length = source.size() - kLastLiterals - position;
      size_t searched = 0;
      while (candidate != kNone && searched++ < kDepth) {
        const size_t previous = static_cast<size_t>(candidate);
        if (previous >= position || position - previous > kWindow) break;
        if (std::memcmp(data + previous, data + position, 4) == 0) {
          if (best.length != 0 && best.length < max_length &&
              data[previous + best.length] != data[position + best.length]) {
            candidate = chain[previous];
            continue;
          }
          size_t length = 4;
          while (length < max_length && data[previous + length] == data[position + length]) {
            ++length;
          }
          if (length > best.length) {
            best = {length, previous};
            if (length == max_length) break;
          }
        }
        candidate = chain[previous];
      }
    }
    matches[position] = best;
    chain[position] = head[hash];
    head[hash] = static_cast<int32_t>(position);
  }

  const auto extension_bytes = [](size_t value) {
    if (value < 15) return size_t{0};
    return (value - 15) / 255 + 1;
  };
  const auto sequence_bytes = [&](size_t literals, size_t match_length) {
    const size_t encoded_match = match_length - 4;
    return size_t{1} + extension_bytes(literals) + literals + 2 +
           extension_bytes(encoded_match);
  };
  const auto final_bytes = [&](size_t literals) {
    return size_t{1} + extension_bytes(literals) + literals;
  };

  struct Choice {
    size_t match_at = 0;
    size_t match_length = 0;
    size_t match_source = 0;
    bool final = true;
  };
  std::vector<size_t> cost(source.size() + 1, kInfinity);
  std::vector<Choice> choices(source.size() + 1);

  for (size_t anchor = source.size() + 1; anchor-- > 0;) {
    cost[anchor] = final_bytes(source.size() - anchor);
    choices[anchor] = {};
    if (anchor == source.size()) continue;

    const size_t last = std::min(source.size() - 1, anchor + kLookahead);
    for (size_t position = anchor; position <= last; ++position) {
      const auto& match = matches[position];
      if (match.length < 4) continue;
      const size_t next = position + match.length;
      if (next > source.size() || cost[next] == kInfinity) continue;
      const size_t sequence = sequence_bytes(position - anchor, match.length);
      if (sequence > kInfinity - cost[next]) continue;
      const size_t candidate = sequence + cost[next];
      if (candidate < cost[anchor]) {
        cost[anchor] = candidate;
        choices[anchor] = {position, match.length, match.source, false};
      }
    }
  }

  if (cost[0] > max_bytes) return false;
  output.reserve(cost[0]);
  size_t anchor = 0;
  while (anchor < source.size()) {
    const auto& choice = choices[anchor];
    if (choice.final) {
      const size_t literals = source.size() - anchor;
      output.push_back(static_cast<unsigned char>(std::min<size_t>(literals, 15) << 4));
      if (literals >= 15) internal::Lz4WriteLength(output, literals - 15);
      output.insert(output.end(), data + anchor, data + source.size());
      break;
    }

    const size_t literals = choice.match_at - anchor;
    const size_t encoded_match = choice.match_length - 4;
    output.push_back(static_cast<unsigned char>(
        (std::min<size_t>(literals, 15) << 4) | std::min<size_t>(encoded_match, 15)));
    if (literals >= 15) internal::Lz4WriteLength(output, literals - 15);
    output.insert(output.end(), data + anchor, data + choice.match_at);
    const size_t distance = choice.match_at - choice.match_source;
    if (distance == 0 || distance > kWindow) {
      output.clear();
      return false;
    }
    output.push_back(static_cast<unsigned char>(distance));
    output.push_back(static_cast<unsigned char>(distance >> 8));
    if (encoded_match >= 15) internal::Lz4WriteLength(output, encoded_match - 15);
    anchor = choice.match_at + choice.match_length;
  }

  if (output.empty() || output.size() > max_bytes) {
    output.clear();
    return false;
  }
  std::vector<unsigned char> decoded(source.size());
  if (!Lz4BlockDecompress(output.data(), output.size(), decoded.data(), decoded.size()) ||
      !std::equal(decoded.begin(), decoded.end(), data)) {
    output.clear();
    return false;
  }
  return true;
}

struct Entry {
  size_t offset;
  size_t header_bytes;
  size_t payload_bytes;
  size_t compressed_bytes;
  uint16_t kind;
  uint32_t architecture;
  uint64_t flags;
  size_t unpacked_bytes;

  size_t PayloadOffset() const {
    return offset + header_bytes;
  }

  size_t End() const {
    return PayloadOffset() + payload_bytes;
  }
};

inline bool Parse(std::span<const unsigned char> bytes, std::vector<Entry>& entries,
                  size_t& end) {
  entries.clear();

  if (bytes.size() < kHeaderBytes || bytes.size() > kMaxFatbinBytes ||
      ReadU32(bytes.data()) != kMagic || ReadU16(bytes.data() + 4) != 1 ||
      ReadU16(bytes.data() + 6) != kHeaderBytes) {
    return false;
  }

  const uint64_t payload_bytes = ReadU64(bytes.data() + 8);
  if (payload_bytes > bytes.size() - kHeaderBytes) return false;

  end = kHeaderBytes + static_cast<size_t>(payload_bytes);
  size_t offset = kHeaderBytes;

  while (offset < end) {
    if (end - offset < 64 || entries.size() == 32) return false;

    const auto* entry = bytes.data() + offset;
    const size_t header_bytes = ReadU32(entry + 4);
    const uint64_t stored_payload_bytes = ReadU64(entry + 8);
    const uint32_t compressed_bytes = ReadU32(entry + 16);
    const uint64_t unpacked_bytes = ReadU64(entry + 56);
    const uint16_t kind = ReadU16(entry);

    if ((kind != 1 && kind != 2) || ReadU16(entry + 2) != 0x101 ||
        header_bytes < 64 || header_bytes > 4096 || header_bytes % 8 != 0 ||
        stored_payload_bytes % 8 != 0 || header_bytes > end - offset ||
        stored_payload_bytes > end - offset - header_bytes ||
        compressed_bytes > stored_payload_bytes || unpacked_bytes > kMaxPtxBytes) {
      return false;
    }

    entries.push_back({
        offset,
        header_bytes,
        static_cast<size_t>(stored_payload_bytes),
        compressed_bytes,
        kind,
        ReadU32(entry + 28),
        ReadU64(entry + 40),
        static_cast<size_t>(unpacked_bytes),
    });
    offset = entries.back().End();
  }

  return offset == end && !entries.empty();
}

}  // namespace mfgunlock::fatbin
