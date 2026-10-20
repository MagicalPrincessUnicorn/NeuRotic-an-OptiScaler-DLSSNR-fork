#include "nr/config/IntentSemantics.h"
#include "../../../external/simpleini/SimpleIni.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace CI = Neurotic::ConfigIntent;

namespace
{
int failures = 0;

void Expect(bool condition, std::string_view label)
{
    if (condition)
        std::cout << "PASS: " << label << '\n';
    else
    {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

std::string Load(std::string_view path)
{
    std::ifstream file(std::string(path), std::ios::binary);
    std::ostringstream data;
    data << file.rdbuf();
    return data.str();
}

CI::IntentSnapshot Snapshot(std::string_view fixture, std::string identity,
                            CI::SchemaVersion schema = CI::LegacyAlphaSchema,
                            std::uint64_t revision = 1)
{
    const auto raw = Load(fixture);
    Expect(!raw.empty(), std::string("fixture readable: ") + std::string(fixture));
    return CI::Capture(CI::ParseLossless(raw), schema, std::move(identity), revision);
}

bool AuthoritativeIs(const CI::MigrationTransaction& transaction, std::string_view expected)
{
    const auto value = transaction.authoritative();
    return value.has_value() && *value == expected;
}

void T54FixedRoutesAndAliases()
{
    for (std::uint32_t route = 0; route <= 2; ++route)
    {
        const std::string raw = "[DlssNr]\nRoute = " + std::to_string(route) + "\n";
        const auto snapshot = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema,
                                          "synthetic-route-" + std::to_string(route), route + 1);
        Expect(snapshot.status == CI::AnalysisStatus::Equivalent,
               "T54 fixed route is an equivalent persisted intent");
        Expect(snapshot.alpha.explicitRoute == route && snapshot.alpha.route == route,
               "T54 route 0/1/2 remains the exact fixed route");
        Expect(snapshot.alpha.routeOrigin == CI::ValueOrigin::Explicit,
               "T54 fixed route remains explicit rather than Auto");
    }

    const auto legacy = Snapshot("tests/nr/config/fixtures/legacy_routes.ini", "root/OptiScaler.ini", CI::LegacyAlphaSchema, 7);
    Expect(legacy.alpha.route == 0 && legacy.alpha.renderingMode == 0,
           "T54 Route plus PerformanceMode preserve Native/fixed placement intent");
    Expect(legacy.alpha.renderingModeOrigin == CI::ValueOrigin::LegacyAlias,
           "T54 PerformanceMode is recorded as a compatibility alias");
    Expect(legacy.FindLast("DlssNr", "Preset") != nullptr &&
           legacy.FindLast("DlssNr", "Intensity") != nullptr &&
           legacy.FindLast("DlssNr", "Style") != nullptr,
           "T54 model, preset and customization values remain in the immutable snapshot");

    const auto hexRoute = CI::Capture(CI::ParseLossless("[DlssNr]\nRoute=0x1\n"),
                                      CI::LegacyAlphaSchema, "legacy-hex-route", 1);
    Expect(hexRoute.status == CI::AnalysisStatus::Equivalent && hexRoute.alpha.route == 1,
           "T54 hexadecimal integer syntax accepted by Alpha remains equivalent");

    const auto retiredMode = CI::Capture(CI::ParseLossless("[DlssNr]\nRenderingMode=2\n"),
                                         CI::LegacyAlphaSchema, "legacy-rendering-mode", 1);
    Expect(retiredMode.alpha.renderingMode == 1,
           "T54 retired RenderingMode 2 retains Alpha clamp-to-Performance meaning");

    const auto precedence = CI::Capture(
        CI::ParseLossless("[DlssNr]\nRenderingMode=0\nPerformanceMode=true\nRunBeforeSR=true\n"),
        CI::LegacyAlphaSchema, "placement-precedence", 1);
    Expect(precedence.alpha.renderingMode == 0 &&
           precedence.alpha.renderingModeOrigin == CI::ValueOrigin::Explicit,
           "T54 explicit RenderingMode wins over legacy aliases");
}

void T54AutoAndSerialMultipass()
{
    const auto autoRoute = CI::Capture(CI::ParseLossless("[DlssNr]\nRoute=auto\n"),
                                       CI::LegacyAlphaSchema, "parser-auto", 1);
    const auto* route = autoRoute.FindLast("DlssNr", "Route");
    Expect(route != nullptr && route->state == CI::ValueState::AutoUnset,
           "T54 parser auto remains unset/default syntax");
    Expect(!autoRoute.alpha.explicitRoute.has_value() && autoRoute.alpha.route == 2 &&
           autoRoute.alpha.routeOrigin == CI::ValueOrigin::SchemaDefault,
           "T54 parser auto does not become future Orchestrator Auto or Inherit");
    Expect(autoRoute.Count(CI::SemanticClass::CapabilityFact) == 0 &&
           autoRoute.Count(CI::SemanticClass::DerivedEffectiveValue) == 0,
           "T54 persisted parsing manufactures no capability or operational effective truth");

    const auto legacy = Snapshot("tests/nr/config/fixtures/legacy_second_layer.ini",
                                 "root/legacy-second-layer.ini", CI::LegacyAlphaSchema, 2);
    Expect(legacy.alpha.multipassEnabled && legacy.alpha.passes == 2,
           "T54 SecondLayer-only profile preserves at least two serial passes");
    Expect(legacy.alpha.multipass == CI::MultipassIntent::LegacySerial,
           "T54 legacy two-layer profile is never widened to shared-source composition");
    Expect(legacy.FindLast("DlssNrLayer2", "TransferStrength") != nullptr,
           "T54 per-layer inherited/custom values remain preserved");

    const auto explicitOff = CI::Capture(
        CI::ParseLossless("[DlssNr]\nMultipassEnabled=false\nSecondLayer=true\nPasses=7\n"),
        CI::LegacyAlphaSchema, "explicit-multipass-off", 1);
    Expect(!explicitOff.alpha.multipassEnabled && explicitOff.alpha.passes == 7,
           "T54 explicit MultipassEnabled=false is not overridden by SecondLayer alias");
}

void T54WhitePointExperimentalAndExternalFg()
{
    const auto alias = Snapshot("tests/nr/config/fixtures/whitepoint_alias.ini",
                                "root/whitepoint-alias.ini");
    Expect(alias.alpha.whitePointSource == 1 &&
           alias.alpha.whitePointOrigin == CI::ValueOrigin::LegacyAlias,
           "T54 retired WhitePointFromExposure fills only an absent new value");

    const auto both = Snapshot("tests/nr/config/fixtures/whitepoint_both.ini",
                               "root/whitepoint-both.ini");
    Expect(both.alpha.whitePointSource == 2 &&
           both.alpha.whitePointOrigin == CI::ValueOrigin::Explicit,
           "T54 explicit WhitePointSource wins when both old and new keys exist");

    const auto experimental = CI::Capture(
        CI::ParseLossless("[DlssNr]\nExperimentalMode=true\nOverrideFgGuardrails=true\n"),
        CI::LegacyAlphaSchema, "legacy-experimental", 1);
    Expect(experimental.FindLast("DlssNr", "ExperimentalMode")->semantic == CI::SemanticClass::UserIntent &&
           experimental.FindLast("DlssNr", "OverrideFgGuardrails")->semantic == CI::SemanticClass::UserIntent,
           "T54 legacy experimental flags remain narrow user intent only");
    Expect(experimental.Count(CI::SemanticClass::CapabilityFact) == 0,
           "T54 experimental intent never asserts support");

    const auto fg = Snapshot("tests/nr/config/fixtures/external_fg.ini",
                             "integration/OptiScaler.ini");
    Expect(fg.FindLast("FrameGen", "FGInput")->semantic == CI::SemanticClass::RestartScopedRequest &&
           fg.FindLast("FrameGen", "FGOutput")->semantic == CI::SemanticClass::RestartScopedRequest &&
           fg.FindLast("FrameGen", "FGNvngxReplacement")->semantic == CI::SemanticClass::RestartScopedRequest,
           "T54 external FG/MFG selectors remain restart-scope candidate requests");
    Expect(fg.FindLast("FrameGen", "FGNvngxReplacement")->rawValue == "arturs",
           "T54 external backend selection remains exact persisted intent");
}

void T54SourceIdentityAndSnapshotIdentity()
{
    const auto root = Snapshot("tests/nr/config/fixtures/integration_identity.ini",
                               "root/OptiScaler.ini", CI::LegacyAlphaSchema, 9);
    const auto integration = Snapshot("tests/nr/config/fixtures/integration_identity.ini",
                                      "integration/OptiScaler.ini", CI::LegacyAlphaSchema, 9);
    Expect(root.sourceIdentity != integration.sourceIdentity,
           "T54 root and integration fixture identities are explicit");
    Expect(root.contentFingerprint != integration.contentFingerprint,
           "T54 identical text from distinct physical sources has distinct content identity");
    const auto nextRevision = CI::Capture(CI::ParseLossless(root.raw), CI::LegacyAlphaSchema,
                                          root.sourceIdentity, 10);
    Expect(root.contentFingerprint == nextRevision.contentFingerprint &&
           root.snapshotIdentity != nextRevision.snapshotIdentity,
           "T54 immutable snapshot revision is distinct from stable content identity");
}

void T55MalformedFutureUnknownAndIdempotence()
{
    const auto malformed = Snapshot("tests/nr/config/fixtures/malformed.ini",
                                    "root/malformed.ini");
    Expect(malformed.status == CI::AnalysisStatus::Malformed,
           "T55 malformed numeric, boolean and enum strings are visible");
    Expect(malformed.raw == Load("tests/nr/config/fixtures/malformed.ini"),
           "T55 malformed input remains byte-exact and recoverable");
    const auto malformedPlan = CI::PrepareMigration(malformed, CI::CurrentSchema);
    Expect(malformedPlan.disposition == CI::MigrationDisposition::RefuseMalformed &&
           !malformedPlan.proposedRaw.has_value(),
           "T55 malformed source is refused rather than destructively rewritten");

    const std::string duplicateRaw = "[DlssNr]\nRoute=0\nRoute=2\n";
    const auto duplicate = CI::Capture(CI::ParseLossless(duplicateRaw), CI::LegacyAlphaSchema,
                                       "root/duplicate-known-key.ini", 1);
    Expect(duplicate.status == CI::AnalysisStatus::Malformed && duplicate.raw == duplicateRaw,
           "T55 duplicate known keys are preserved but refused as ambiguous input");
    Expect(CI::PrepareMigration(duplicate, CI::CurrentSchema).disposition ==
               CI::MigrationDisposition::RefuseMalformed,
           "T55 ambiguous duplicate known keys cannot be destructively migrated");

    const auto unknownAuto = CI::Capture(
        CI::ParseLossless("[UnknownFutureSection]\nOpaqueAuto=auto\n"),
        CI::LegacyAlphaSchema, "root/unknown-auto.ini", 1);
    const auto* opaqueAuto = unknownAuto.FindLast("UnknownFutureSection", "OpaqueAuto");
    Expect(opaqueAuto != nullptr && opaqueAuto->semantic == CI::SemanticClass::UnknownFutureValue &&
           opaqueAuto->state == CI::ValueState::Explicit && !unknownAuto.alpha.parserAutoObserved,
           "T55 unknown future value named auto remains opaque rather than default syntax");

    const auto unknown = Snapshot("tests/nr/config/fixtures/unknown_roundtrip.ini",
                                  "root/unknown-roundtrip.ini");
    Expect(unknown.status == CI::AnalysisStatus::Equivalent &&
           unknown.Count(CI::SemanticClass::UnknownFutureValue) == 2,
           "T55 unknown keys remain explicit unknown/future values");
    const auto noRewrite = CI::PrepareMigration(unknown, CI::CurrentSchema);
    Expect(noRewrite.disposition == CI::MigrationDisposition::NoRewriteRequired &&
           noRewrite.originalRaw == unknown.raw && !noRewrite.proposedRaw.has_value(),
           "T55 supported Alpha intent requires no lossy rewrite");
    const auto again = CI::PrepareMigration(unknown, CI::CurrentSchema);
    Expect(noRewrite == again,
           "T55 repeated migration planning is idempotent");

    const auto future = CI::Capture(CI::ParseLossless("[DlssNr]\nRoute=0\nFutureThing=keep\n"), {2, 0},
                                    "root/future-schema.ini", 3);
    Expect(future.status == CI::AnalysisStatus::UnsupportedFutureSchema,
           "T55 unsupported future schema is classified explicitly");
    Expect(!future.alpha.explicitRoute.has_value() &&
           future.alpha.routeOrigin == CI::ValueOrigin::SchemaDefault,
           "T55 unsupported future schema is never interpreted as Alpha effective intent");
    const auto futurePlan = CI::PrepareMigration(future, CI::CurrentSchema);
    Expect(futurePlan.disposition == CI::MigrationDisposition::RefuseUnsupportedFutureSchema &&
           futurePlan.originalRaw == future.raw && !futurePlan.proposedRaw.has_value(),
           "T55 future schema preserves original raw data and refuses destructive interpretation");

    const auto downgrade = CI::PrepareMigration(unknown, {0, 8});
    Expect(downgrade.disposition == CI::MigrationDisposition::RefuseNonRepresentable,
           "T55 downgrade refuses unknown/new semantics not representable in an old schema");

    const auto futureTarget = CI::PrepareMigration(unknown, {2, 0});
    Expect(futureTarget.disposition == CI::MigrationDisposition::RefuseUnsupportedFutureSchema,
           "T55 helper refuses migration into a schema version it does not implement");

    const auto oversized = CI::Capture(
        CI::ParseLossless("[DlssNr]\nRoute=2147483648\n"), CI::LegacyAlphaSchema,
        "root/oversized-int.ini", 1);
    Expect(oversized.status == CI::AnalysisStatus::Malformed,
           "T55 integer values outside Alpha stoi range are malformed rather than reinterpreted");
}

void T55TransactionAndExactRestore()
{
    const std::string original = "[DlssNr]\nRoute=1\nFutureThing=keep\n";
    const std::string proposed = "[DlssNr]\nRoute=1\nFutureThing=keep\n; staged equivalent\n";

    CI::MigrationTransaction transaction(original, proposed);
    Expect(transaction.state() == CI::TransactionState::OriginalAuthoritative &&
           AuthoritativeIs(transaction, original),
           "T55 transaction begins with untouched original authoritative");
    Expect(!transaction.Stage(true),
           "T55 staging cannot occur before backup confirmation");
    Expect(transaction.ConfirmBackup(true) && transaction.backupRetained(),
           "T55 backup is retained before staging");
    Expect(transaction.Stage(true) && transaction.Validate(true),
           "T55 replacement is staged and validated separately");
    Expect(transaction.Commit(false) == false && AuthoritativeIs(transaction, original),
           "T55 commit failure leaves original authoritative");
    Expect(transaction.Rollback(true) && AuthoritativeIs(transaction, original) &&
           transaction.authoritative() == transaction.original(),
           "T55 rollback restores the untouched original exactly");

    CI::MigrationTransaction success(original, proposed);
    Expect(success.ConfirmBackup(true) && success.Stage(true) && success.Validate(true) && success.Commit(true),
           "T55 validated staged replacement can commit");
    Expect(AuthoritativeIs(success, proposed) && success.backupRetained(),
           "T55 committed replacement retains backup for restoration");
    Expect(!success.Rollback(false) && AuthoritativeIs(success, proposed),
           "T55 failed rollback never pretends the original became authoritative");
    Expect(success.Rollback(true) && AuthoritativeIs(success, original),
           "T55 post-commit rollback restores exact original bytes");

    CI::MigrationTransaction partial(original, proposed);
    Expect(!partial.ConfirmBackup(false) && AuthoritativeIs(partial, original),
           "T55 partial migration failure before staging preserves original");

    CI::MigrationTransaction uncertain(original, proposed);
    Expect(uncertain.ConfirmBackup(true) && uncertain.Stage(true) && uncertain.Validate(true),
           "T55 indeterminate-commit fixture reaches validated staging");
    Expect(!uncertain.Commit(CI::CommitOutcome::Indeterminate) &&
           uncertain.authority() == CI::AuthorityState::Unknown &&
           !uncertain.authoritative().has_value(),
           "T55 indeterminate commit never guesses which file is authoritative");
    Expect(!uncertain.Rollback(false) && uncertain.authority() == CI::AuthorityState::Unknown,
           "T55 failed recovery from indeterminate commit remains unknown");
    Expect(uncertain.Rollback(true) && AuthoritativeIs(uncertain, original),
           "T55 successful recovery from indeterminate commit restores the backup");
}
}


// Literal expectations are independent of the candidate parser. The grammar and
// signed-32-bit bounds are grounded in Alpha Config::readInt/readUInt; readUInt
// converts the successful signed int to uint32_t before per-key clamping.
void F2AlphaIntegerCompatibility()
{
    struct Case { const char* text; std::int64_t signedValue; std::uint64_t unsignedValue; };
    const Case valid[] = {
        {"1", 1, 1}, {"+1", 1, 1}, {"+0001", 1, 1}, {"01", 1, 1}, {"010", 10, 10},
        {"0", 0, 0}, {"+0", 0, 0}, {"-0", 0, 0}, {"-000", 0, 0},
        {"-1", -1, 4294967295ULL}, {"0x1", 1, 1}, {"0X01", 1, 1},
        {"0x7fffffff", 2147483647LL, 2147483647ULL},
        {"2147483647", 2147483647LL, 2147483647ULL},
        {"+2147483647", 2147483647LL, 2147483647ULL},
        {"-2147483648", -2147483648LL, 2147483648ULL}
    };
    for (const auto& item : valid)
    {
        const std::string label = std::string("F2 Alpha-valid integer ") + item.text;
        Expect(CI::Detail::ParseSigned(item.text) == item.signedValue, label + " signed");
        Expect(CI::Detail::ParseUnsigned(item.text) == item.unsignedValue, label + " unsigned");
        const std::string raw = std::string("[DlssNr]\nRenderingMode=") + item.text + "\n";
        const auto snapshot = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema,
                                          "F2-rendering-mode", 1);
        const auto* value = snapshot.FindLast("DlssNr", "RenderingMode");
        const int expected = item.signedValue > 0 ? 1 : 0;
        Expect(snapshot.status == CI::AnalysisStatus::Equivalent &&
               snapshot.alpha.renderingModeOrigin == CI::ValueOrigin::Explicit &&
               snapshot.alpha.renderingMode == expected && value &&
               value->state == CI::ValueState::Explicit && value->rawValue == item.text &&
               snapshot.raw == raw, label + " explicit RenderingMode and unchanged raw bytes");
        const std::string routeRaw = std::string("[DlssNr]\nRoute=") + item.text + "\n";
        const auto route = CI::Capture(CI::ParseLossless(routeRaw), CI::LegacyAlphaSchema,
                                      "F2-route", 1);
        const std::uint32_t expectedRoute = item.unsignedValue > 2 ? 2u :
                                           static_cast<std::uint32_t>(item.unsignedValue);
        Expect(route.status == CI::AnalysisStatus::Equivalent &&
               route.alpha.routeOrigin == CI::ValueOrigin::Explicit &&
               route.alpha.explicitRoute == expectedRoute && route.raw == routeRaw,
               label + " Alpha unsigned conversion before Route clamp");
    }
    for (const char* text : {"", "+", "-", "++1", "+-1", "-+1", "--1", "+ 1", "- 1",
                             "+0x1", "-0x1", "0x+1", "0x-1", "0x", "0X", "0xg1", "1tail",
                             "1.0", "1e0", "2147483648", "+2147483648", "-2147483649",
                             "0x80000000", "0xffffffff", "18446744073709551616"})
    {
        const std::string label = std::string("F2 Alpha-invalid integer [") + text + "]";
        Expect(!CI::Detail::ParseSigned(text).has_value() &&
               !CI::Detail::ParseUnsigned(text).has_value(), label);
        const std::string raw = std::string("[DlssNr]\nRenderingMode=") + text + "\n";
        const auto snapshot = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema,
                                          "F2-malformed", 1);
        Expect(snapshot.status == CI::AnalysisStatus::Malformed && snapshot.raw == raw &&
               CI::PrepareMigration(snapshot, CI::CurrentSchema).disposition ==
                   CI::MigrationDisposition::RefuseMalformed, label + " preserved and migration refused");
    }
    for (const auto& raw : {std::string("[DlssNr]\nRenderingMode=+1\nRenderingMode=1\n"),
                            std::string("[DlssNr]\nRoute=+1\nroute=1\n")})
    {
        const auto snapshot = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema,
                                          "F2-duplicate", 1);
        Expect(snapshot.status == CI::AnalysisStatus::Malformed && snapshot.raw == raw &&
               CI::PrepareMigration(snapshot, CI::CurrentSchema).disposition ==
                   CI::MigrationDisposition::RefuseMalformed,
               "F2 accepting a plus sign does not weaken duplicate refusal");
    }
    const std::string raw = "[DlssNr]\nRenderingMode=+1\nRoute=+1\nFutureKey=0x-1\n";
    const auto future = CI::Capture(CI::ParseLossless(raw), {2, 0}, "F2-future", 1);
    Expect(future.status == CI::AnalysisStatus::UnsupportedFutureSchema && future.raw == raw &&
           !future.alpha.explicitRoute.has_value() &&
           future.alpha.renderingModeOrigin == CI::ValueOrigin::SchemaDefault &&
           CI::PrepareMigration(future, CI::CurrentSchema).disposition ==
               CI::MigrationDisposition::RefuseUnsupportedFutureSchema,
           "F2 plus-valid data never activates Alpha intent for a future schema");
    const auto known = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema, "F2-unknown", 1);
    const auto* opaque = known.FindLast("DlssNr", "FutureKey");
    Expect(known.status == CI::AnalysisStatus::Equivalent && opaque &&
           opaque->semantic == CI::SemanticClass::UnknownFutureValue &&
           opaque->rawValue == "0x-1" && known.raw == raw,
           "F2 unknown values stay opaque even when they resemble malformed integers");
}

int main()
{
    // Differential expectations come from the exact Alpha SimpleIni dependency.
    for (const auto& raw : {std::string("\xEF\xBB\xBF[DlssNr]\nRoute=0\n"),
                           std::string("[DlssNr] ; user comment\nRoute=0\n"),
                           std::string("[ DlssNr ] ignored suffix\r\nRoute=0\r\n"),
                           std::string("[DlssNr] # comment\nRoute=0\n"),
                           std::string("[DlssNr]\rRoute=0\r"),
                           std::string("\xEF\xBB\xBF[DlssNr]\r\nRoute=0\r\n")})
    {
        CSimpleIniA alpha;
        Expect(alpha.LoadData(raw.data(), raw.size()) >= 0,
               "T54 Alpha reader accepts legacy section/encoding fixture");
        const auto expected = alpha.GetLongValue("DlssNr", "Route", 2);
        Expect(expected == 0, "T54 independent Alpha fixture has explicit Native route");
        const auto snapshot = CI::Capture(CI::ParseLossless(raw), CI::LegacyAlphaSchema,
                                          "section-encoding-differential", 1);
        Expect(snapshot.status == CI::AnalysisStatus::Equivalent &&
               snapshot.alpha.explicitRoute.has_value() &&
               snapshot.alpha.route == static_cast<std::uint32_t>(expected),
               "T54 BOM, section suffix and newline semantics match Alpha");
        Expect(snapshot.raw == raw && CI::PrepareMigration(snapshot, CI::CurrentSchema).disposition ==
                   CI::MigrationDisposition::NoRewriteRequired,
               "T54 section/encoding compatibility preserves every input byte");
    }
    F2AlphaIntegerCompatibility();
    T54FixedRoutesAndAliases();
    T54AutoAndSerialMultipass();
    T54WhitePointExperimentalAndExternalFg();
    T54SourceIdentityAndSnapshotIdentity();
    T55MalformedFutureUnknownAndIdempotence();
    T55TransactionAndExactRestore();
    if (failures != 0)
    {
        std::cerr << failures << " NR-CFG-001 candidate checks failed\n";
        return 1;
    }
    std::cout << "PASS: NR-CFG-001 authored T54/T55 CPU candidate cases\n";
    return 0;
}
