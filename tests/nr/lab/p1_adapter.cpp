// Provider-free metadata fixtures; none of the fixture files is executable.
#include "../../../tools/nr/lab/P1Adapter.h"
#include <windows.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using lab::json;
namespace fs = std::filesystem;
static std::string utf8(const fs::path& p)
{
    auto u = p.u8string();
    return { u.begin(), u.end() };
}
static void put(const fs::path& p, const std::string& s)
{
    std::ofstream f(p, std::ios::binary);
    f << s;
    if (!f)
        throw std::runtime_error("fixture write");
}
static json read(const fs::path& p)
{
    std::ifstream f(p);
    return json::parse(f);
}
static std::string bytes(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return { (std::istreambuf_iterator<char>(f)), {} };
}
static void require(bool b, const std::string& s)
{
    if (!b)
        throw std::runtime_error(s);
}
struct Fixture
{
    fs::path root, dir;
    json modules, launch, r, s, p;
    int exit = 0;
    explicit Fixture(const fs::path& base) : root(base), dir(base / L"receipt")
    {
        fs::create_directories(dir);
        const char* roles[] = { "patched_provider", "ngx_core", "nr_forwarder", "runner", "supervisor" };
        const char* hashes[] = { "a0b9d75686960138c7d529e7985a19b1a067fa7fe498a3ed790f5b2f834bb429",
                                 "2bc2dad2e38a0b7cee969a5e60bb21d9f01759ea8ab89e2c949f3dbfb2d91905",
                                 "3a693baad6c7bafff6eb6e993c37bb2257af4734e2c4df9a917d756f2964a940",
                                 "527aa9f431539da8e151d5434d1d5e611d973f601d8e970790882624554146b0",
                                 "0834c2d60725ac5902257b3b78dd161ad26d1c0290dbf1e47cc14add5b8c8142" };
        for (int i = 0; i < 5; ++i)
        {
            auto path = root / (std::string(roles[i]) + ".fixture");
            put(path, roles[i]);
            if (i < 3)
                modules[roles[i]] = { { "path", utf8(path) }, { "sha256", hashes[i] } };
            else
            {
                launch[std::string(roles[i]) + "_path"] = utf8(path);
                launch[std::string(roles[i]) + "_sha256"] = hashes[i];
            }
        }
        r = json::parse(
            R"({"schema_version":3,"receipt_kind":"P1_FEATURE18","status":"FEATURE18_BASELINE_OBSERVED","stage":"complete","error":"","requested_feature_id":18,"process_id":1234,"create_count":1,"evaluate_count":1,"created_handle":"0x1000","evaluated_handle":"0x1000","parameter_path":"core-capability/forwarder-dlssnr-v1","float_slot":1,"module_load":{"provider":true,"core":true,"forwarder":true},"adapter":{"name":"NVIDIA GeForce RTX 4080","vendor_id":4318,"device_id":9988,"luid":123,"driver_version_raw":123456,"driver_version_known":true},"ngx_results":{"init":1,"get_parameters":1,"provider_init":1,"create_feature":1,"evaluate_feature":1},"retained_baseline":{"queue_type":"DIRECT","queue_count":1,"queue_address":100,"list_address":200,"allocator_address":300,"parameters_address":123,"feature_address":4096,"list_closed":true,"submitted":true,"fence_signaled":true,"fence_completed":true,"submitted_fence_value":1,"completed_fence_value":1,"readback_observed":true,"output_changed_from_zero":true,"format":"R16G16B16A16_FLOAT","input_extent":[64,64],"output_extent":[64,64]},"readback":{"file":"output.rgba16f","byte_count":32768,"feature_id":18,"feature_handle":"0x1000","classification":"OUTPUT_CHANGED","sha256":"53f9c5abec5adaac3372ed6f0cb0f203941edab83bff591fc0e94fccd7070e19"},"device_removed_reason":0})");
        r["loaded_modules"] = json::array();
        for (int i = 0; i < 3; ++i)
            r["loaded_modules"].push_back(
                { { "role", roles[i] }, { "path", modules[roles[i]]["path"] }, { "sha256_at_load", hashes[i] } });
        s = json::parse(
            R"({"schema_version":1,"process_status":"exited","child_exit_code":0,"child_pid":1234,"timed_out":false,"unhandled_exception":null,"child_drained":true,"stdout_file":"runner-stdout.txt","stderr_file":"runner-stderr.txt","dump_file":null})");
        p = launch;
        p.update({ { "schema_version", 3 },
                   { "requested_feature_id", 18 },
                   { "status", "PREFLIGHT_PASS" },
                   { "modules", modules },
                   { "receipt_output_path", utf8(dir) } });
        std::string pixels(32768, '\0');
        pixels[0] = 1;
        put(dir / L"output.rgba16f", pixels);
        put(dir / L"runner-stdout.txt", "");
        put(dir / L"runner-stderr.txt", "");
    }
    void save()
    {
        put(dir / L"runner.json", r.dump());
        put(dir / L"supervisor.json", s.dump());
        put(dir / L"preflight.json", p.dump());
    }
};
int wmain(int argc, wchar_t** argv)
{
    try
    {
        const fs::path root =
            argc > 1 ? fs::absolute(argv[1])
                     : fs::temp_directory_path() / (L"lab-p1-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                                    std::to_wstring(GetTickCount64()));
        require(!fs::exists(root), "test root must be fresh");
        fs::create_directories(root);
        int count = 0;
        auto check = [&](const char* name, const std::function<void(Fixture&)>& mutate,
                         const char* want = "P1_RECEIPT_INVALID", bool halted = true)
        {
            Fixture f(root / std::to_string(++count));
            mutate(f);
            f.save();
            auto before = bytes(f.dir / L"runner.json");
            auto result = lab::ValidateP1(f.dir, f.modules, f.launch, f.exit);
            require(result.at("classification") == want, std::string(name) + ": " + result.dump());
            require(result.at("success") == std::string(want).starts_with("P1_BASELINE_OBSERVED"),
                    std::string(name) + ": success");
            require(result.at("qualification") == false, "qualification must remain false");
            require(result.at("gpu_session_halted") == halted, std::string(name) + ": halt");
            require(bytes(f.dir / L"runner.json") == before, "raw receipt bytes changed");
            std::cout << "PASS " << name << '\n';
        };
        check("valid schema3", [](auto&) {}, "P1_BASELINE_OBSERVED", false);
        auto refusal = [](Fixture& f)
        {
            f.r["status"] = "INCONCLUSIVE";
            f.r["stage"] = "entry";
            f.r["error"] = "PROVIDER_HASH_MISMATCH";
            f.r["create_count"] = 0;
            f.r["evaluate_count"] = 0;
            f.r["created_handle"] = "0x0";
            f.r["evaluated_handle"] = "0x0";
            f.r["readback"]["feature_handle"] = "0x0";
            f.r["readback"]["classification"] = "NOT_OBSERVED";
            f.r["readback"]["sha256"] = "";
            for (auto n : { "list_closed", "submitted", "fence_signaled", "fence_completed", "readback_observed",
                            "output_changed_from_zero" })
                f.r["retained_baseline"][n] = false;
            for (auto n : { "submitted_fence_value", "completed_fence_value", "feature_address" })
                f.r["retained_baseline"][n] = 0;
            for (auto n : { "provider", "core", "forwarder" })
                f.r["module_load"][n] = false;
            for (auto& m : f.r["loaded_modules"])
            {
                m["path"] = "";
                m["sha256_at_load"] = "";
            }
            fs::remove(f.dir / L"output.rgba16f");
            f.s["child_exit_code"] = 1;
        };
        check("pre-entry refusal does not halt", refusal, "P1_PROVIDER_REFUSED", false);
        check(
            "initialization failure after module entry halts",
            [&](auto& f)
            {
                refusal(f);
                f.r["error"] = "NGX_INIT_FAILED";
                f.r["module_load"]["core"] = true;
                f.r["loaded_modules"][1]["path"] = f.modules["ngx_core"]["path"];
                f.r["loaded_modules"][1]["sha256_at_load"] = f.modules["ngx_core"]["sha256"];
            },
            "P1_PROVIDER_REFUSED", true);
        check(
            "current DRED v1 shape",
            [](auto& f)
            {
                f.r["dred"] = json::parse(
                    R"({"capture_version":1,"available":false,"breadcrumb_query_hresult":null,"page_fault_query_hresult":null,"breadcrumb_nodes_present":false,"breadcrumb_node_count":0,"nodes_truncated":false,"cycle_detected":false,"page_fault_observed":false,"page_fault_va":0,"interpretation":"GPU progress evidence; next unconfirmed operation is not proof of fault origin","nodes":[]})");
            },
            "P1_BASELINE_OBSERVED", false);
        auto dred2 = [](Fixture& f)
        {
            f.r["dred"] = json::parse(
                R"({"capture_version":2,"available":false,"interface_version":0,"context_capture_enabled":true,"breadcrumb_query_hresult":null,"page_fault_query_hresult":null,"breadcrumb_nodes_present":false,"breadcrumb_node_count":0,"nodes_truncated":false,"cycle_detected":false,"page_fault_observed":false,"page_fault_va":0,"interpretation":"GPU progress evidence; next unconfirmed operation is not proof of fault origin","nodes":[]})");
            f.r["phase_markers"] = { "W03.P1.Upload.Begin",           "W03.P1.Upload.End",
                                     "W03.P1.Create.Begin",           "W03.P1.Create.End",
                                     "W03.P1.Evaluate.Begin",         "W03.P1.Evaluate.End",
                                     "W03.P1.OutputTransition.Begin", "W03.P1.OutputTransition.End",
                                     "W03.P1.ReadbackCopy.Begin",     "W03.P1.ReadbackCopy.End" };
        };
        check("DRED v2 and exact phase markers", dred2, "P1_BASELINE_OBSERVED", false);
        // Match the bound W03-P1 WriteDred emitter, including populated contexts.
        auto dredContext = [&](Fixture& f)
        {
            dred2(f);
            auto& d = f.r["dred"];
            d["available"] = true;
            d["interface_version"] = 1;
            d["breadcrumb_query_hresult"] = 0;
            d["breadcrumb_nodes_present"] = true;
            d["breadcrumb_node_count"] = 1;
            d["nodes"] = json::array({json::parse(
                R"({"command_list_name":"P1 retained list","command_queue_name":"P1 direct queue","command_list_address":"0x100","command_queue_address":"0x200","names_truncated":false,"breadcrumb_count":2,"completed_count":2,"progress_valid":true,"last_completed_index":1,"next_unconfirmed_index":null,"history_present":true,"history_status":"CAPTURED_WINDOW","window_truncated":false,"operations":[{"index":0,"code":0,"name":"SETMARKER"},{"index":1,"code":0,"name":"SETMARKER"}],"contexts_supported":true,"contexts_present":true,"context_count":1,"contexts_truncated":false,"contexts":[{"index":0,"index_valid":true,"text":"W03.P1.Upload.Begin","text_present":true,"text_truncated":false}]})")});
        };
        check("DRED v2 emitted text_present", dredContext, "P1_BASELINE_OBSERVED", false);
        auto contextAt = [](Fixture& f) -> json& { return f.r["dred"]["nodes"][0]["contexts"][0]; };
        check("DRED null context pointer", [&](auto& f) {
            dredContext(f); auto& c=contextAt(f); c["text_present"]=false; c["text"]="";
        }, "P1_BASELINE_OBSERVED", false);
        check("DRED nonnull empty context", [&](auto& f) {
            dredContext(f); contextAt(f)["text"]="";
        }, "P1_BASELINE_OBSERVED", false);
        check("DRED truncated context", [&](auto& f) {
            dredContext(f); contextAt(f)["text_truncated"]=true; contextAt(f)["text"]=std::string(256,'x');
        }, "P1_BASELINE_OBSERVED", false);
        check("DRED text_present missing", [&](auto& f) { dredContext(f); contextAt(f).erase("text_present"); });
        check("DRED text_present numeric", [&](auto& f) { dredContext(f); contextAt(f)["text_present"]=1; });
        check("DRED text_present null", [&](auto& f) { dredContext(f); contextAt(f)["text_present"]=nullptr; });
        check("DRED absent text contradiction", [&](auto& f) { dredContext(f); contextAt(f)["text_present"]=false; });
        check("DRED absent text truncation contradiction", [&](auto& f) {
            dredContext(f); auto& c=contextAt(f); c["text_present"]=false; c["text"]=""; c["text_truncated"]=true;
        });
        check("DRED unknown context field", [&](auto& f) { dredContext(f); contextAt(f)["unexpected"]=true; });
        check("DRED populated device-hung receipt remains failure", [&](auto& f) {
            dredContext(f);
            f.r["status"]="INCONCLUSIVE"; f.r["stage"]="submit";
            f.r["error"]="FENCE_OR_DEVICE_COMPLETION_UNKNOWN";
            f.r["device_removed_reason"]=-2005270522;
            f.r["retained_baseline"]["completed_fence_value"]=UINT64_MAX;
            for(auto key:{"fence_completed","readback_observed","output_changed_from_zero"})
                f.r["retained_baseline"][key]=false;
            f.r["readback"]["classification"]="NOT_OBSERVED"; f.r["readback"]["sha256"]="";
            f.s["child_exit_code"]=1; fs::remove(f.dir/L"output.rgba16f");
        }, "P1_DEVICE_REMOVED", true);
        check("DRED v2 boolean integer",
              [&](auto& f)
              {
                  dred2(f);
                  f.r["dred"]["interface_version"] = false;
              });
        check("phase marker wrong order",
              [&](auto& f)
              {
                  dred2(f);
                  f.r["phase_markers"][1] = "W03.P1.Create.Begin";
              });
        check("complete marker omission",
              [&](auto& f)
              {
                  dred2(f);
                  f.r["phase_markers"].erase(9);
              });
        check("false is not exit zero", [](auto& f) { f.s["child_exit_code"] = false; });
        check("float is not exit integer", [](auto& f) { f.s["child_exit_code"] = 0.0; });
        check("schema2 remains unmodified and rejected", [](auto& f) { f.r["schema_version"] = 2; });
        check("feature18 required", [](auto& f) { f.r["requested_feature_id"] = 1; });
        check("stale process rejected", [](auto& f) { f.r["process_id"] = 1235; });
        check("handle substituted", [](auto& f) { f.r["evaluated_handle"] = "0x2000"; });
        check("handle type", [](auto& f) { f.r["created_handle"] = 4096; });
        check("repeated evaluation", [](auto& f) { f.r["evaluate_count"] = 2; });
        check("false result", [](auto& f) { f.r["ngx_results"]["evaluate_feature"] = false; });
        check("failed result", [](auto& f) { f.r["ngx_results"]["evaluate_feature"] = 0; });
        check("readback region", [](auto& f) { f.r["retained_baseline"]["input_extent"] = { 64, 32 }; });
        check("readback region numeric string",
              [](auto& f) { f.r["retained_baseline"]["output_extent"] = { "64", 64 }; });
        check("readback path escape", [](auto& f) { f.r["readback"]["file"] = "../output.rgba16f"; });
        check("readback bytes missing", [](auto& f) { fs::remove(f.dir / L"output.rgba16f"); });
        check("readback bytes corrupt", [](auto& f) { put(f.dir / L"output.rgba16f", std::string(32768, '\0')); });
        check("fence completion unknown", [](auto& f) { f.r["retained_baseline"]["fence_completed"] = false; });
        check("fence sentinel", [](auto& f) { f.r["retained_baseline"]["completed_fence_value"] = UINT64_MAX; });
        check("module hash substituted",
              [](auto& f) { f.r["loaded_modules"][0]["sha256_at_load"] = std::string(64, 'a'); });
        check("module false is not loaded", [](auto& f) { f.r["module_load"]["provider"] = false; });
        check("module duplicate role", [](auto& f) { f.r["loaded_modules"][0]["role"] = "ngx_core"; });
        check("preflight hash substituted", [](auto& f) { f.p["runner_sha256"] = std::string(64, 'a'); });
        check("preflight stale output", [](auto& f) { f.p["receipt_output_path"] = utf8(f.root); });
        check("preflight boolean feature", [](auto& f) { f.p["requested_feature_id"] = true; });
        check("selected adapter mismatch", [](auto& f) { f.launch["adapter_luid"] = 124; });
        check(
            "selected adapter matches", [](auto& f) { f.launch["adapter_luid"] = 123; }, "P1_BASELINE_OBSERVED", false);
        check("boolean vendor", [](auto& f) { f.r["adapter"]["vendor_id"] = true; });
        check("device removed", [](auto& f) { f.r["device_removed_reason"] = -2005270523; });
        check("unknown field", [](auto& f) { f.r["unexpected"] = true; });
        check("undrained child", [](auto& f) { f.s["child_drained"] = false; });
        check(
            "timeout",
            [](auto& f)
            {
                f.s["process_status"] = "timeout";
                f.s["timed_out"] = true;
                f.s["child_exit_code"] = 124;
                f.exit = 124;
            },
            "P1_TIMEOUT");
        check(
            "native exception",
            [](auto& f)
            {
                f.s["process_status"] = "native_exception";
                f.s["child_exit_code"] = 3221225477u;
                f.s["unhandled_exception"] = { { "code", 3221225477u } };
                f.exit = 3;
            },
            "P1_NATIVE_EXCEPTION");
        check("exception address boolean",
              [](auto& f)
              {
                  f.s["process_status"] = "native_exception";
                  f.s["child_exit_code"] = 3221225477u;
                  f.s["unhandled_exception"] = { { "code", 3221225477u }, { "address", false } };
                  f.exit = 3;
              });
        for (const auto* file : { "runner.json", "preflight.json", "supervisor.json" })
        {
            Fixture f(root / std::to_string(++count));
            f.save();
            std::ifstream in(f.dir / file);
            std::string raw((std::istreambuf_iterator<char>(in)), {});
            in.close();
            raw.insert(1, "\"schema_version\":3,");
            put(f.dir / file, raw);
            auto result = lab::ValidateP1(f.dir, f.modules, f.launch, 0);
            require(result.at("classification") == "P1_RECEIPT_INVALID", "duplicate key accepted");
            require(result.at("reason").get<std::string>().find("DUPLICATE") != std::string::npos, "duplicate reason");
            std::cout << "PASS duplicate " << file << '\n';
        }
        Fixture f(root / L"argv");
        auto a = lab::P1Arguments(f.modules, f.dir);
        require(a.size() == 12 && a[0] == L"--provider" && a[2] == L"--core" && a[4] == L"--core-sha256" &&
                    a[6] == L"--forwarder" && a[8] == L"--forwarder-sha256" && a[10] == L"--receipt" &&
                    a[11] == f.dir.wstring(),
                "fixed recipe argv");
        ++count;
        if (argc > 2)
        {
            const fs::path parity = argv[2];
            const auto trusted = read(parity / L"case-1/preflight.json");
            json launch;
            for (const auto* k : { "runner_path", "runner_sha256", "supervisor_path", "supervisor_sha256" })
                launch[k] = trusted.at(k);
            int pc = 0;
            for (const auto& e : fs::directory_iterator(parity))
            {
                if (!e.is_directory() || !fs::exists(e.path() / L"test-result.json"))
                    continue;
                auto expected = read(e.path() / L"test-result.json");
                auto got = lab::ValidateP1(e.path(), trusted.at("modules"), launch,
                                           expected.at("supervisor_exit_code").get<int>());
                require(got.at("classification") == expected.at("classification"),
                        "PS parity " + utf8(e.path()) + ": " + got.dump());
                require(got.at("process_success") == expected.at("process_success"), "PS process parity");
                ++pc;
            }
            require(pc == 55, "expected all 55 accepted PS fixtures");
            std::cout << "PARITY_CASES=" << pc << '\n';
        }
        std::cout << "NATIVE_CASES=" << count << " ROOT=" << utf8(root) << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
