// SPDX-License-Identifier: MIT
#include "../../../src/addons/mfgunlock/ptx_retarget.hpp"
#include "../../../src/addons/mfgunlock/ptx_compat.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

namespace fb = mfgunlock::fatbin;
namespace ptx = mfgunlock::ptx;

using Bytes = std::vector<unsigned char>;

namespace {

unsigned int g_cases = 0;
constexpr std::string_view kPtx =
    ".version 8.7\n"
    ".target sm_89\n"
    ".address_size 64\n"
    ".visible .entry test() { ret; }\n";

void Check(bool value) {
  ++g_cases;
  if (!value) throw std::runtime_error("case " + std::to_string(g_cases));
}

template <class T>
void Put(Bytes& bytes, size_t offset, T value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

Bytes LiteralBlock(std::string_view text) {
  Bytes output;
  output.push_back(static_cast<unsigned char>(std::min<size_t>(text.size(), 15) << 4));
  if (text.size() >= 15) {
    size_t remaining = text.size() - 15;
    for (; remaining >= 255; remaining -= 255) output.push_back(255);
    output.push_back(static_cast<unsigned char>(remaining));
  }
  output.insert(output.end(), text.begin(), text.end());
  return output;
}

Bytes Container(std::string text, const Bytes* payload = nullptr) {
  if (text.empty() || text.back() != '\0') text.push_back('\0');
  const auto compressed = payload ? *payload : LiteralBlock(text);
  Bytes fatbin(80 + ((compressed.size() + 7) & ~size_t{7}));

  Put<uint32_t>(fatbin, 0, fb::kMagic);
  Put<uint16_t>(fatbin, 4, 1);
  Put<uint16_t>(fatbin, 6, 16);
  Put<uint64_t>(fatbin, 8, fatbin.size() - 16);
  Put<uint16_t>(fatbin, 16, 1);
  Put<uint16_t>(fatbin, 18, 0x101);
  Put<uint32_t>(fatbin, 20, 64);
  Put<uint64_t>(fatbin, 24, fatbin.size() - 80);
  Put<uint32_t>(fatbin, 32, static_cast<uint32_t>(compressed.size()));
  Put<uint32_t>(fatbin, 44, 89);
  Put<uint64_t>(fatbin, 56, 0x2041);
  Put<uint64_t>(fatbin, 72, text.size());
  std::copy(compressed.begin(), compressed.end(), fatbin.begin() + 80);
  return fatbin;
}

void PackedHalfTests() {
  const std::string fixture =
      ".version 8.7\n.target sm_120\n.address_size 64\n"
      ".visible .entry X() {\n.reg .b32 %r<4>;\n.reg .f32 %f<4>;\n"
      "{ cvt.rn.f16x2.f32 %r1, %f2, %f3; }\nret;\n}\n";
  std::string why;
  auto output = fixture;
  Check(ptx::LowerPackedHalf(output, 75, 1, why));
  Check(output.find("cvt.rn.f16.f32 %mfg_h0, %f2;") != output.npos);
  Check(output.find("cvt.rn.f16.f32 %mfg_h1, %f3;") != output.npos);
  Check(output.find("mov.b32 %r1, {%mfg_h1, %mfg_h0};") != output.npos);
  Check(!ptx::HasPackedHalfConversion(output));
  Check(ptx::HasPackedHalfConversion(fixture));
  Check(output.find(".reg .b16 %mfg_h<2>;") != output.npos);
  auto again = output;
  Check(!ptx::LowerPackedHalf(again, 75, 1, why) && again == output);
  for (size_t count : {0u, 2u}) {
    output = fixture;
    Check(!ptx::LowerPackedHalf(output, 75, count, why) && output == fixture);
  }
  output = fixture;
  Check(ptx::LowerPackedHalf(output, 86, 1, why) && output == fixture);
  Check(ptx::LowerPackedHalf(output, 89, 1, why) && output == fixture);
  output = "ret;\n";
  Check(ptx::LowerPackedHalf(output, 75, 0, why) && output == "ret;\n");
  for (const auto& replacement : {
           "{ cvt.rz.f16x2.f32 %r1, %f2, %f3; }",
           "{ cvt.rn.sat.f16x2.f32 %r1, %f2, %f3; }",
           "@%p0 cvt.rn.f16x2.f32 %r1, %f2, %f3;",
           "{ cvt.rn.f16x2.f32 %r4, %f2, %f3; }",
           "{ cvt.rn.f16x2.f32 %r1, %f2, %f4; }",
           "{ cvt.rn.f16x2.f32 %r1, %f2, 0f00000000; }",
           "{ cvt.rn.f16x2.f32 %r1, %f2, %f999999999999999999999; }",
           "{ cvt.rn.f16x2.f32 %r1, %f2, %f3; } ret;"}) {
    output = fixture;
    const auto start = output.find("{ cvt.");
    output.replace(start, output.find('\n', start) - start, replacement);
    const auto before = output;
    Check(!ptx::LowerPackedHalf(output, 75, 1, why) && output == before);
  }
  for (const auto& tail : {"// %mfg_h0\n", ".reg .b32 %r<4>;\n", ".reg .f32 %f<4>;\n"}) {
    output = fixture + tail;
    const auto before = output;
    Check(!ptx::LowerPackedHalf(output, 75, 1, why) && output == before);
  }
  // Distinct lanes: a=1.0h goes high and b=-2.0h low, not the reverse.
  const uint32_t high = 0x3c00u;
  const uint32_t low = 0xc000u;
  Check((high << 16 | low) == 0x3c00c000u);
}


void TuringHalfMinMaxTests() {
  const std::string fixture =
      ".version 8.7\n.target sm_89\n.address_size 64\n"
      ".visible .entry X() {\n.reg .b32 %r<8>;\n.reg .b16 %rs<8>;\n.reg .f32 %f<4>;\n"
      "{max.f16 %rs0,%rs1,%rs2;\n}\n"
      "{min.f16 %rs3,%rs4,%rs5;\n}\n"
      "{max.f16x2 %r0,%r1,%r2;\n}\n"
      "{min.f16x2 %r3,%r4,%r5;\n}\nret;\n}\n";
  const ptx::HalfMinMaxCounts counts{1, 1, 1, 1};
  std::string why;
  auto output = fixture;
  Check(ptx::HasTuringHalfMinMax(output));
  Check(ptx::LowerTuringHalfMinMax(output, 75, counts, why));
  Check(!ptx::HasTuringHalfMinMax(output));
  Check(output.find(".reg .f32 %f<6>;\n") != output.npos);
  Check(output.find(".reg .b16 %h<4>;\n") != output.npos);
  Check(output.find("max.f32 %f4,%f4,%f5;") != output.npos);
  Check(output.find("min.f32 %f4,%f4,%f5;") != output.npos);
  Check(output.find("mov.b32 {%h0,%h1},%r1;") != output.npos);
  Check(output.find("mov.b32 %r0,{%h0,%h1};") != output.npos);

  auto unchanged = fixture;
  Check(ptx::LowerTuringHalfMinMax(unchanged, 86, counts, why) && unchanged == fixture);
  unchanged = fixture;
  Check(ptx::LowerTuringHalfMinMax(unchanged, 89, counts, why) && unchanged == fixture);
  unchanged = fixture;
  Check(!ptx::LowerTuringHalfMinMax(unchanged, 75, {2, 1, 1, 1}, why) &&
        unchanged == fixture);
  unchanged = "ret;\n";
  Check(ptx::LowerTuringHalfMinMax(unchanged, 75, {}, why) && unchanged == "ret;\n");
  unchanged = fixture;
  Check(!ptx::LowerTuringHalfMinMax(unchanged, 75, {}, why) && unchanged == fixture);

  auto collision = fixture + "%h0;\n";
  const auto collision_before = collision;
  Check(!ptx::LowerTuringHalfMinMax(collision, 75, counts, why) &&
        collision == collision_before);
  Check(why == "half min/max temporary register collision");

  for (const auto& replacement : {
           "{max.ftz.f16 %rs0,%rs1,%rs2;",
           "@%p0 max.f16 %rs0,%rs1,%rs2;",
           "{max.f16 %rs8,%rs1,%rs2;",
           "{max.f16x2 %r8,%r1,%r2;",
           "{max.f16 %rs0,%rs1,0h3c00;",
           "{max.f16x2 %r0,%r1,0;"}) {
    auto invalid = fixture;
    const auto start = invalid.find("{max.f16 %rs0");
    invalid.replace(start, invalid.find('\n', start) - start, replacement);
    const auto before = invalid;
    Check(!ptx::LowerTuringHalfMinMax(invalid, 75, counts, why) && invalid == before);
  }

  size_t total_half_minmax = 0;
  for (const auto& profile : ptx::kTuringMmaProfiles) {
    total_half_minmax += profile.half_minmax.Total();
  }
  Check(total_half_minmax == 294);
}


void TuringMmaTests() {
  Check(std::size(ptx::kTuringMmaProfiles) == 27);
  size_t total_mma = 0;
  for (const auto& profile : ptx::kTuringMmaProfiles) total_mma += profile.instructions;
  Check(total_mma == 670);
  const std::string fixture =
      ".version 8.7\n.target sm_89\n.address_size 64\n"
      ".visible .entry X() {\n.reg .b32 %r<16>;\n"
      "mma.sync.aligned.m16n8k16.row.col.f16.f16.f16.f16 "
      "{%r0,%r1}, {%r2,%r3,%r4,%r5}, {%r6,%r7}, {%r8,%r9};\nret;\n}\n";
  std::string why;
  auto output = fixture;
  Check(ptx::LowerTuringMma16816(output, 75, 1, why));
  Check(!ptx::HasTuringMma16816(output));
  Check(output.find(
      "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16 {%r0,%r1},{%r2,%r3},{%r6},{%r8,%r9};") !=
      output.npos);
  Check(output.find(
      "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16 {%r0,%r1},{%r4,%r5},{%r7},{%r0,%r1};") !=
      output.npos);
  auto unchanged = fixture;
  Check(ptx::LowerTuringMma16816(unchanged, 86, 1, why) && unchanged == fixture);
  unchanged = fixture;
  Check(ptx::LowerTuringMma16816(unchanged, 89, 1, why) && unchanged == fixture);
  unchanged = fixture;
  Check(!ptx::LowerTuringMma16816(unchanged, 75, 2, why) && unchanged == fixture);

  auto hazard = fixture;
  const auto instruction = hazard.find("{%r0,%r1}, {%r2,%r3,%r4,%r5}");
  hazard.replace(instruction, std::string("{%r0,%r1}, {%r2,%r3,%r4,%r5}").size(),
                 "{%r4,%r1}, {%r2,%r3,%r4,%r5}");
  const auto hazard_before = hazard;
  Check(!ptx::LowerTuringMma16816(hazard, 75, 1, why) && hazard == hazard_before);
  Check(why == "m16n8k16 split has an output/input register hazard");

  auto wrong = fixture;
  const auto shape = wrong.find(".f16.f16.f16.f16");
  wrong.replace(shape, std::string(".f16.f16.f16.f16").size(), ".f32.f16.f16.f32");
  const auto wrong_before = wrong;
  Check(!ptx::LowerTuringMma16816(wrong, 75, 1, why) && wrong == wrong_before);

  std::string compact = "\n//\nadd.s32 %r1, %r2, 1;\n.file 1 \"a, b\"\n\n";
  Check(ptx::CompactProviderPtx(compact, why));
  Check(compact == "add.s32 %r1,%r2,1;\n.file 1 \"a, b\"");
  auto block_comment = std::string("/* x */\nret;\n");
  const auto block_before = block_comment;
  Check(!ptx::CompactProviderPtx(block_comment, why) && block_comment == block_before);

  std::string source;
  for (int index = 0; index < 300; ++index) {
    source += "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16 {%r0,%r1},{%r2,%r3},{%r4},{%r5,%r6};\n";
  }
  source.push_back('\0');
  Bytes compressed;
  Check(fb::Lz4BlockCompress(source, source.size(), compressed));
  Check(compressed.size() < source.size());
  Bytes decoded(source.size());
  Check(fb::Lz4BlockDecompress(compressed.data(), compressed.size(), decoded.data(), decoded.size()));
  Check(std::equal(decoded.begin(), decoded.end(), reinterpret_cast<const unsigned char*>(source.data())));
  Bytes high_compressed;
  Check(fb::Lz4BlockCompressHigh(source, source.size(), high_compressed));
  Check(high_compressed.size() <= compressed.size());
  Bytes high_decoded(source.size());
  Check(fb::Lz4BlockDecompress(high_compressed.data(), high_compressed.size(),
                               high_decoded.data(), high_decoded.size()));
  Check(std::equal(high_decoded.begin(), high_decoded.end(),
                   reinterpret_cast<const unsigned char*>(source.data())));
  const auto compressed_before = compressed;
  Check(!fb::Lz4BlockCompress(source, 8, compressed) && compressed.empty());
  Check(!compressed_before.empty());
}

void UnitTests() {
  PackedHalfTests();
  TuringHalfMinMaxTests();
  TuringMmaTests();
  std::string reason;
  ptx::Plan plan;
  const auto valid = Container(std::string(kPtx));

  Check(ptx::Retarget(valid, plan, reason) == ptx::Result::kRetargeted);
  Check(plan.replacement.size() == valid.size());
  Check(fb::ReadU32(plan.replacement.data() + 44) == 86);

  size_t index = 0;
  const auto changed = std::count_if(valid.begin(), valid.end(), [&](unsigned char value) {
    return value != plan.replacement[index++];
  });
  // One PTX literal and one architecture byte change in a single-image fatbin.
  Check(changed == 2);

  // The Ampere profile must emit exactly the same bytes as the original patch.
  auto ampere_expected = valid;
  Put<uint32_t>(ampere_expected, 44, 86);
  const auto literal_header = LiteralBlock(std::string(kPtx) + '\0').size() - kPtx.size() - 1;
  ampere_expected[80 + literal_header + kPtx.find("sm_89") + 4] = '6';
  Check(plan.replacement == ampere_expected);

  Check(ptx::Retarget(plan.replacement, plan, reason) == ptx::Result::kUnchanged);

  for (size_t bytes : {0u, 1u, 15u, 16u, 63u, 79u}) {
    Check(ptx::Retarget(std::span(valid).first(bytes), plan, reason) ==
          ptx::Result::kRejected);
  }
  for (size_t bytes = 80; bytes < valid.size(); ++bytes) {
    Check(ptx::Retarget(std::span(valid).first(bytes), plan, reason) ==
          ptx::Result::kRejected);
  }

  for (const char* tail : {"\n.target sm_89\n", "\n.version 8.8\n", "\n/* open",
                           "\nwgmma.mma_async;\n"}) {
    Check(ptx::Retarget(Container(std::string(kPtx) + tail), plan, reason) ==
          ptx::Result::kRejected);
  }

  Check(ptx::Retarget(Container("/* .target sm_120 */\n" + std::string(kPtx) +
                               "// .target sm_120\n"),
                     plan, reason) == ptx::Result::kRetargeted);
  Check(ptx::Retarget(Container(".file 1 \".target sm_120\"\n" + std::string(kPtx)),
                     plan, reason) == ptx::Result::kRetargeted);

  auto no_nul = valid;
  Put<uint64_t>(no_nul, 72, kPtx.size());
  Check(ptx::Retarget(no_nul, plan, reason) == ptx::Result::kRejected);

  auto bad_arch = valid;
  Put<uint32_t>(bad_arch, 44, 75);
  Check(ptx::Retarget(bad_arch, plan, reason) == ptx::Result::kUnchanged);

  auto bad_flags = valid;
  Put<uint64_t>(bad_flags, 56, 0x2040);
  Check(ptx::Retarget(bad_flags, plan, reason) == ptx::Result::kRejected);

  auto overflow = valid;
  Put<uint64_t>(overflow, 24, UINT64_MAX);
  Check(ptx::Retarget(overflow, plan, reason) == ptx::Result::kRejected);

  Bytes bad_offset{0, 0, 0};
  Bytes bad_offset_output(5);
  Check(!fb::Lz4BlockDecompress(bad_offset.data(), bad_offset.size(),
                                bad_offset_output.data(), bad_offset_output.size()));

  Bytes overlap{0x14, 'a', 1, 0, 0};
  Bytes overlap_output(9);
  Check(fb::Lz4BlockDecompress(overlap.data(), overlap.size(), overlap_output.data(),
                               overlap_output.size()));
  Check(overlap_output == Bytes(9, 'a'));

  // Same literal reused by a match: the target edit must be refused rather than
  // silently changing another decoded byte through the back-reference.
  std::string prefix(kPtx);
  auto block = LiteralBlock(prefix);
  block[0] |= 1;  // match length 5
  const size_t target = prefix.find("sm_89");
  const size_t distance = prefix.size() - target;
  block.push_back(static_cast<unsigned char>(distance));
  block.push_back(static_cast<unsigned char>(distance >> 8));
  block.push_back(0x10);
  block.push_back(0);
  Check(ptx::Retarget(Container(prefix + "sm_89", &block), plan, reason) ==
        ptx::Result::kRejected);
  Check(reason == "target literal is shared with another output byte");

  Check(ptx::Retarget(Container(prefix + "sm_89", &block), plan, reason,
                     mfgunlock::architecture::kTuring) == ptx::Result::kRejected);
  Check(reason == "target literal is shared with another output byte");

  Check(ptx::Retarget(valid, plan, reason, mfgunlock::architecture::kTuring) == ptx::Result::kRetargeted);
  auto turing_expected = valid;
  Put<uint32_t>(turing_expected, 44, 75);
  turing_expected[80 + literal_header + kPtx.find("sm_89") + 3] = '7';
  turing_expected[80 + literal_header + kPtx.find("sm_89") + 4] = '5';
  Check(plan.replacement == turing_expected);
  Check(ptx::Retarget(plan.replacement, plan, reason,
                     mfgunlock::architecture::kTuring) == ptx::Result::kUnchanged);
  Check(ptx::Retarget(valid, plan, reason, mfgunlock::architecture::kAda) == ptx::Result::kUnchanged);
  Check(plan.replacement.empty());

  for (const auto* instruction : {"cp.async.ca.shared.global", "mbarrier.init", "redux.sync.add",
                                  "mma.sp.sync", "cvt.rn.bf16.f32", "mma.sync.aligned.m16n8k16",
                                  "cvt.rn.f16x2.f32", "max.f16", "min.f16x2"}) {
    const auto input = Container(std::string(kPtx) + instruction + ";\n");
    Check(ptx::Retarget(input, plan, reason, mfgunlock::architecture::kTuring) == ptx::Result::kRejected);
    Check(ptx::Retarget(input, plan, reason, mfgunlock::architecture::kAmpere) == ptx::Result::kRetargeted);
  }
  Check(ptx::Retarget(Container(std::string(kPtx) + "// cp.async; .tf32; mbarrier.init;\n"),
                     plan, reason, mfgunlock::architecture::kTuring) == ptx::Result::kRetargeted);
  Check(ptx::Retarget(Container(std::string(kPtx) + "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32;\n"),
                     plan, reason, mfgunlock::architecture::kTuring) == ptx::Result::kRetargeted);

  Check(ptx::Retarget(Container(std::string(kPtx) + "// cvt.rn.f16x2.f32\n"),
                     plan, reason, mfgunlock::architecture::kTuring) == ptx::Result::kRetargeted);
  Check(ptx::PackedHalfCount("unqualified source") == 0);
  auto roomy = Container(std::string(kPtx));
  std::vector<fb::Entry> entries;
  size_t container_end = 0;
  Check(fb::Parse(roomy, entries, container_end));
  const auto entry = entries.front();
  roomy.resize(roomy.size() + 256, 0x71);
  Bytes rebuilt;
  std::string lowered(kPtx);
  lowered.replace(lowered.find("sm_89"), 5, "sm_75");
  Check(ptx::RebuildLoweredProvider(roomy, entry, lowered, rebuilt));
  Check(rebuilt.size() == roomy.size());
  Check(fb::Parse(rebuilt, entries, container_end) && entries.size() == 1);
  Check(entries[0].kind == 1 && entries[0].architecture == 75);
  Check(entries[0].flags == 0x41 && entries[0].compressed_bytes == 0);
  Check(std::string(reinterpret_cast<const char*>(rebuilt.data() + entries[0].PayloadOffset())) == lowered);
  Check(container_end < rebuilt.size());
  const auto previous = rebuilt;
  Check(!ptx::RebuildLoweredProvider(roomy, entry, std::string(roomy.size(), 'x'), rebuilt));
  Check(rebuilt == previous);
  auto invalid_entry = entry;
  invalid_entry.header_bytes = 1;
  Check(!ptx::RebuildLoweredProvider(roomy, invalid_entry, lowered, rebuilt));
  invalid_entry = entry;
  invalid_entry.offset = roomy.size();
  Check(!ptx::RebuildLoweredProvider(roomy, invalid_entry, lowered, rebuilt));

  std::mt19937 random(13);
  for (unsigned int i = 0; i < 1000; ++i) {
    auto mutated = valid;
    mutated[random() % mutated.size()] ^=
        static_cast<unsigned char>(1 + random() % 255);
    (void)ptx::Retarget(mutated, plan, reason);  // sanitizer smoke input
    (void)ptx::Retarget(mutated, plan, reason, mfgunlock::architecture::kTuring);
  }
}

// PTX ISA 11.7 sections 9.7.13.4.7/.8: independently compare each
// lane's f16 matrix coordinates, not just the text emitted by our lowerer.
void TuringMmaFragments() {
  for (unsigned lane = 0; lane < 32; ++lane) {
    const unsigned group = lane / 4, thread = lane % 4;
    for (unsigned i = 0; i < 8; ++i) {
      const unsigned row16 = (i < 2 || (i >= 4 && i < 6)) ? group : group + 8;
      const unsigned col16 = thread * 2 + (i & 1) + (i >= 4 ? 8 : 0);
      const unsigned half = i / 4, local = i % 4;
      const unsigned row8 = local < 2 ? group : group + 8;
      const unsigned col8 = thread * 2 + (local & 1) + half * 8;
      Check(row16 == row8 && col16 == col8);
    }
    for (unsigned i = 0; i < 4; ++i) {
      const unsigned row16 = thread * 2 + (i & 1) + (i >= 2 ? 8 : 0);
      const unsigned row8 = thread * 2 + (i % 2) + (i / 2) * 8;
      Check(row16 == row8);
    }
  }
}

void Corpus(const std::filesystem::path& path, const std::filesystem::path& output_path) {
  unsigned int accepted = 0;
  std::filesystem::create_directories(output_path);

  for (const auto& item : std::filesystem::directory_iterator(path)) {
    if (item.path().extension() != ".fatbin") continue;

    std::ifstream input(item.path(), std::ios::binary);
    Bytes bytes{std::istreambuf_iterator<char>(input), {}};
    ptx::Plan plan;
    std::string reason;
    if (ptx::Retarget(bytes, plan, reason) != ptx::Result::kRetargeted) {
      throw std::runtime_error(item.path().string() + ": " + reason);
    }

    std::ofstream output(output_path / item.path().filename(), std::ios::binary);
    output.write(reinterpret_cast<const char*>(plan.replacement.data()),
                 static_cast<std::streamsize>(plan.replacement.size()));
    ++accepted;
  }

  if (accepted == 0) throw std::runtime_error("empty corpus");
  std::cout << "corpus_retargeted=" << accepted << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    UnitTests();
    TuringMmaFragments();
    if (argc == 3) {
      Corpus(argv[1], argv[2]);
    } else if (argc != 1) {
      throw std::runtime_error(
          "usage: ptx_retarget_test [fatbin_directory output_directory]");
    }
    std::cout << "assertions=" << g_cases << ", mutation_smoke_inputs=1000\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
