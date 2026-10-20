#include "../../../tools/nr/lab/OwnedD3D12.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <windows.h>

static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "output root required");
        const std::filesystem::path root = argv[1];
        Require(!std::filesystem::exists(root), "test output must be fresh");
        std::filesystem::create_directories(root);
        // Catches silent hardware selection, ignored unknown options and mistyped parameters.
        for (const auto& bad : std::vector<lab::json>{nullptr, lab::json::array(),
             {{"adapter", "warp"}}, {{"adapter", true}, {"adapter_luid", nullptr}},
             {{"adapter", "physical"}, {"adapter_luid", nullptr}},
             {{"adapter", "physical"}, {"adapter_luid", "bad"}},
             {{"adapter", "warp"}, {"adapter_luid", "00000000:00000000"}},
             {{"adapter", "warp"}, {"adapter_luid", nullptr}, {"extra", 1}}}) {
            auto r = lab::RunOwnedD3D12(bad, root / L"invalid", false);
            Require(!r.at("success").get<bool>() && r.at("reason") == "INVALID_PARAMETERS", "invalid parameter accepted");
        }
        Require(!std::filesystem::exists(root / L"invalid"), "invalid input touched outputs");
        for (bool cross : {false, true}) {
            auto dir = root / (cross ? L"cross" : L"same");
            auto r = lab::RunOwnedD3D12({{"adapter", "warp"}, {"adapter_luid", nullptr}}, dir, cross);
            std::ofstream(dir.string() + ".json") << r.dump(2);
            Require(r.at("success").get<bool>(), r.dump().c_str());
            Require(r.at("adapter").at("kind") == "warp", "WARP silently replaced");
            Require(r.at("completion").at("observed").get<bool>(), "missing completion");
            Require(r.at("queues").size() == (cross ? 2u : 1u), "wrong queue identity count");
            const auto& journal = r.at("journal");
            Require(journal.size() == (cross ? 5u : 4u), "missing submission journal");
            for (size_t i = 0; i != journal.size(); ++i)
                Require(journal[i].at("sequence") == i + 1, "noncontiguous journal");
            if (cross) {
                Require(r["queues"][0]["identity"] != r["queues"][1]["identity"], "cross-queue identities collapsed");
                Require(journal[2]["operation"] == "Wait" && journal[2]["queue"] == r["queues"][1]["identity"] &&
                        journal[2]["fence"] == r["fences"][0]["identity"], "consumer dependency missing");
            }
            for (const auto& fence : r.at("fences"))
                Require(fence.at("observed").get<bool>() && fence.at("completed_value") == "1", "fence completion not exact");
            Require(r["negative_controls"]["decoy_unchanged"].get<bool>(), "decoy was changed");
            Require(r["memory"]["planned_allocation_bytes"].get<unsigned long long>() <=
                    r["memory"]["allocation_cap_bytes"].get<unsigned long long>(), "memory cap exceeded");
            Require(lab::ValidateOwnedReadback(dir / L"output.bin").at("success").get<bool>(), "real readback rejected");
            Require(!lab::ValidateOwnedReadback(dir / L"preimage.bin").at("success").get<bool>(), "no-op positive oracle");
            Require(!lab::ValidateOwnedReadback(dir / L"decoy.bin").at("success").get<bool>(), "wrong-target positive oracle");
            auto corrupt = dir / L"corrupted.bin";
            std::filesystem::copy_file(dir / L"output.bin", corrupt);
            std::fstream f(corrupt, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(777); f.put('\xFF'); f.close();
            Require(!lab::ValidateOwnedReadback(corrupt).at("success").get<bool>(), "corrupt readback accepted");
            auto truncated = dir / L"truncated.bin";
            std::filesystem::copy_file(dir / L"output.bin", truncated);
            std::filesystem::resize_file(truncated, 16383);
            Require(!lab::ValidateOwnedReadback(truncated).at("success").get<bool>(), "truncated readback accepted");
            Require(!lab::RunOwnedD3D12({{"adapter", "warp"}, {"adapter_luid", nullptr}}, dir, cross).at("success").get<bool>(), "stale output overwritten");
        }
        Require(!lab::ValidateOwnedReadback(root / L"missing.bin").at("success").get<bool>(), "missing readback accepted");
        std::cout << "PASS: strict parameters; real WARP same/cross queue; completion; no-op/wrong-target/corrupt/truncated/missing oracle; fresh-output refusal\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
