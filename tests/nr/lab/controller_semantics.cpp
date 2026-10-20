// Reuse the existing provider-free receipt fixture, not a second receipt builder.
// Its executable entry is renamed; this test only invokes Fixture and ValidateP1.
#define wmain P1FixtureSuiteMain
#include "p1_adapter.cpp"
#undef wmain
#include "../../../tools/nr/lab/LabValidation.h"

int wmain(int argc, wchar_t** argv)
{
    try
    {
        const auto root = argc > 1 ? fs::absolute(argv[1])
                                  : fs::temp_directory_path() / (L"lab-controller-semantics-" + lab::Wide(lab::Nonce()));
        require(!fs::exists(root), "test root must be fresh");
        Fixture fixture(root);
        // Exact P1 FNV for a 32768-byte image [1, 0, ..., 0], independently
        // calculated with arbitrary-precision arithmetic modulo 2^64.
        fixture.r["retained_baseline"]["output_fnv1a64"] = UINT64_C(3940652666407494436);
        fixture.r["adapter"]["luid"] = UINT64_MAX;
        fixture.r["adapter"]["driver_version_raw"] = UINT64_C(9007199254740993);
        fixture.launch["adapter_luid"] = UINT64_MAX;
        fixture.save();
        const auto originalJson = bytes(fixture.dir / L"runner.json");
        const auto originalPixels = bytes(fixture.dir / L"output.rgba16f");
        const auto jsonHash = lab::Sha256(originalJson), pixelsHash = lab::Sha256(originalPixels);
        const auto native = lab::ValidateP1(fixture.dir, fixture.modules, fixture.launch, 0);
        require(native.at("success") == true, "valid uint64 P1 fixture rejected: " + native.dump());
        require(native.at("observations").at("adapter").at("luid").is_number_unsigned(),
                "native adapter must retain exact uint64 observations");

        json request = { { "case_id", "NLAB-NR-001" }, { "run_id", "synthetic-controller-roundtrip" },
                         { "case_revision", 1 }, { "variant_id", "retained" }, { "request_nonce", "test-only-nonce" },
                         { "package_sha256", std::string(64, 'a') },
                         { "runtime_config_sha256", std::string(64, 'b') } };
        const auto requestFile = root / L"case-request.json";
        lab::WriteJson(requestFile, request);
        auto outer = lab::InitialResult(request, requestFile);
        outer["execution"] = { { "state", "EXITED_NORMAL" }, { "pid", 1234 }, { "raw_exit_code", "0" },
                               { "exception_code", nullptr }, { "feature_id", 18 },
                               { "retirement_mode", "PROCESS_EXIT_RETAINED" } };
        outer["verdict"] = "OBSERVED";
        outer["reason"] = "Synthetic schema regression only; no provider executed";
        outer["evidence_completeness"] = "COMPLETE";
        outer["observations"]["p1_validation"] = native;
        bool rejected = false;
        try { (void)lab::StrictJson(outer.dump()); }
        catch (const std::exception& error) { rejected = std::string(error.what()) == "INTEGER_RANGE"; }
        require(rejected, "regression must reproduce unprojected INTEGER_RANGE failure");

        outer["observations"]["p1_validation"] = lab::TransportObservation(native);
        const auto repository = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        const auto schema = lab::ReadJson(repository / L"tools/nr/lab/design/schemas/case-result.schema.json");
        lab::CheckSchema(outer, schema);
        const auto saved = root / L"outer-case-result.json";
        lab::WriteJson(saved, outer);
        const auto reread = lab::ReadJson(saved);
        lab::CheckSchema(reread, schema);
        require(reread == outer, "outer receipt changed during disk roundtrip");
        const auto& observations = reread.at("observations").at("p1_validation").at("observations");
        require(observations.at("adapter").at("luid") == "18446744073709551615", "uint64 maximum lost");
        require(observations.at("adapter").at("driver_version_raw") == "9007199254740993", "integer rounded");
        require(observations.at("retained_baseline").at("output_fnv1a64") == "3940652666407494436",
                "raw FNV association lost");
        require(observations.at("readback").at("byte_count").is_number_integer() &&
                    observations.at("readback").at("byte_count") == 32768,
                "ordinary integer observation unnecessarily changed");
        require(reread.at("observations").at("p1_validation").at("success").is_boolean(), "boolean changed");
        require(bytes(fixture.dir / L"runner.json") == originalJson &&
                    lab::FileHash(fixture.dir / L"runner.json") == jsonHash,
                "raw schema3 receipt changed");
        require(bytes(fixture.dir / L"output.rgba16f") == originalPixels &&
                    lab::FileHash(fixture.dir / L"output.rgba16f") == pixelsHash,
                "raw image bytes changed");
        require(lab::ValidateP1(fixture.dir, fixture.modules, fixture.launch, 0) == native,
                "native result changed after transport persistence");
        std::cout << "CONTROLLER_SEMANTICS_PASS native_uint64_valid unprojected_rejected projected_schema_roundtrip "
                     "decimal_precision_preserved raw_json_and_image_unchanged\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
