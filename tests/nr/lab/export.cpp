#include "../../../tools/nr/lab/LabExport.h"
#include <iostream>
#include <map>
using namespace lab;
static void Put(const fs::path& p, const std::string& s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
    Require(f.good(), "FIXTURE_WRITE");
}
static uint32_t N(const std::string& s, size_t at, size_t n)
{
    Require(at + n <= s.size(), "ZIP_SHORT");
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i)
        v |= uint32_t(static_cast<unsigned char>(s[at + i])) << (8 * i);
    return v;
}
static std::map<std::string, std::string> Entries(const fs::path& path)
{
    auto raw = ReadText(path, 256 * 1024 * 1024);
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (N(raw, at, 4) == 0x04034b50)
    {
        Require(N(raw, at + 6, 2) == 0x800 && N(raw, at + 8, 2) == 0, "ZIP_FLAGS");
        auto size = N(raw, at + 18, 4), names = N(raw, at + 26, 2), extra = N(raw, at + 28, 2);
        Require(N(raw, at + 22, 4) == size, "ZIP_STORED_SIZE");
        auto name = raw.substr(at + 30, names);
        Require(extra == 0 && at + 30 + names + size <= raw.size(), "ZIP_ENTRY_BOUND");
        Require(out.emplace(name, raw.substr(at + 30 + names, size)).second, "ZIP_DUPLICATE");
        at += 30 + names + size;
    }
    Require(N(raw, at, 4) == 0x02014b50 && N(raw, raw.size() - 22, 4) == 0x06054b50, "ZIP_DIRECTORY");
    Require(N(raw, raw.size() - 12, 2) == out.size(), "ZIP_ENTRY_COUNT");
    return out;
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(review_export::Crc32("123456789") == 0xcbf43926u, "ZIP_STANDARD_CRC32_VECTOR");
        auto root =
            argc > 1 ? fs::absolute(argv[1]) : fs::temp_directory_path() / (L"lab-export-test-" + Wide(Nonce()));
        Require(!fs::exists(root), "FRESH_ROOT_REQUIRED");
        auto run = root / L"run";
        fs::create_directories(run);
        const std::string privateText = "read failed C:\\Users\\Private Person\\secret.dll then "
                                        "file:///C:/private/model.bin and \\\\server\\private share\\file";
        Put(run / L"SUMMARY.md", "# Summary\n" + privateText + "\n");
        Put(run / L"SUMMARY.json", json({ { "reason", privateText } }).dump());
        Put(run / L"RUN_PLAN.json",
            json({ { "nested", json::array({ { { "path", "C:/Users/Private Person/model.dll" } } }) } }).dump());
        auto caseDir = run / L"cases/NLAB-NR-001-1";
        Put(caseDir / L"case-result.json", json({ { "status", "NOT_RUN" }, { "reason", privateText } }).dump());
        Put(caseDir / L"validation.json", "{}");
        Put(caseDir / L"events.jsonl", json({ { "path", "file:///C:/private" } }).dump() + "\n");
        Put(caseDir / L"runner.json",
            json({ { "process_id", 1234 }, { "path", "C:/private/provider.dll" }, { "luid", UINT64_MAX } }).dump());
        Put(caseDir / L"owned/output.bin", std::string(16384, '\x12'));
        Put(caseDir / L"owned/decoy.bin", "MZdummy renamed proprietary payload");
        Put(caseDir / L"owned/preimage.bin", std::string(16 * 1024 * 1024 + 1, 'X'));
        Put(caseDir / L"private.dll", "MZsecret dll");
        Put(caseDir / L"model.json", "PRIVATE_MODEL_PAYLOAD");
        Put(caseDir / L"crash.dmp", std::string(17 * 1024 * 1024, 'D'));
        Put(caseDir / L"runner-stdout.txt", "PRIVATE_RAW_LOG");
        Put(run / L"LOCAL_RUNTIME.json", "PRIVATE_CONFIG");
        auto original = ReadText(caseDir / L"runner.json"), summary = ReadText(run / L"SUMMARY.md");
        ExportReview(run, root / L"review.zip");
        auto e = Entries(root / L"review.zip");
        Require(e.contains("EXPORT_MANIFEST.json") && e.contains("projections/SUMMARY.json") &&
                    e.contains("projections/cases/NLAB-NR-001-1/runner.json") &&
                    e.contains("cases/NLAB-NR-001-1/owned/output.bin"),
                "FIXED_EXPORT_ENTRIES");
        Require(ReadText(caseDir / L"runner.json") == original && ReadText(run / L"SUMMARY.md") == summary,
                "ORIGINAL_CHANGED");
        for (const auto& [name, data] : e)
        {
            Require(data.find("Private Person") == std::string::npos &&
                        data.find("private/provider") == std::string::npos &&
                        data.find("file:///") == std::string::npos && data.find("server\\") == std::string::npos,
                    "PATH_LEAK:" + name);
            Require(data.find("PRIVATE_MODEL_PAYLOAD") == std::string::npos &&
                        data.find("PRIVATE_RAW_LOG") == std::string::npos &&
                        data.find("PRIVATE_CONFIG") == std::string::npos && data.find("MZdummy") == std::string::npos,
                    "PAYLOAD_LEAK");
        }
        Require(!e.contains("cases/NLAB-NR-001-1/owned/decoy.bin") &&
                    !e.contains("cases/NLAB-NR-001-1/owned/preimage.bin"),
                "REFUSED_BINARY_EXPORTED");
        auto manifest = StrictJson(e.at("EXPORT_MANIFEST.json"));
        Require(manifest.at("original_hashes_apply_to") == "local_original_bytes_only", "PROJECTION_HASH_CLAIM");
        bool hash = false;
        for (auto& a : manifest.at("entries"))
            if (a.at("archive_path") == "projections/cases/NLAB-NR-001-1/runner.json")
                hash = a.at("original_sha256") == Sha256(original);
        Require(hash, "ORIGINAL_HASH_NOT_RETAINED");
        Require(manifest.at("omitted_local_artifacts").size() >= 7, "OMISSION_INVENTORY");
        bool refused = false;
        auto zipBefore = FileHash(root / L"review.zip");
        try
        {
            ExportReview(run, root / L"review.zip");
        }
        catch (const std::exception&)
        {
            refused = true;
        }
        Require(refused && FileHash(root / L"review.zip") == zipBefore, "OVERWRITE_ALLOWED");
        auto incomplete = root / L"incomplete";
        fs::create_directories(incomplete);
        refused = false;
        try
        {
            ExportReview(incomplete, root / L"missing.zip");
        }
        catch (const std::exception&)
        {
            refused = true;
        }
        Require(refused && !fs::exists(root / L"missing.zip"), "INCOMPLETE_PUBLISHED");
        Put(run / L"SUMMARY.json", "{\"status\":1,\"status\":2}");
        refused = false;
        try
        {
            ExportReview(run, root / L"duplicate.zip");
        }
        catch (const std::exception&)
        {
            refused = true;
        }
        Require(refused && !fs::exists(root / L"duplicate.zip"), "DUPLICATE_JSON_PUBLISHED");
        std::cout << "EXPORT_TESTS_PASS ZIP=" << PathText(root / L"review.zip") << " ENTRIES=" << e.size() << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
