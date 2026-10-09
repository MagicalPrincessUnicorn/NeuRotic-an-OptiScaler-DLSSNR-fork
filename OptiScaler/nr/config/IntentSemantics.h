#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Neurotic::ConfigIntent
{
struct SchemaVersion
{
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    bool operator==(const SchemaVersion&) const = default;
};

inline constexpr SchemaVersion LegacyAlphaSchema {0, 96};
inline constexpr SchemaVersion CurrentSchema {1, 0};

constexpr bool NewerThan(SchemaVersion left, SchemaVersion right) noexcept
{
    return left.major > right.major || (left.major == right.major && left.minor > right.minor);
}

constexpr bool OlderThan(SchemaVersion left, SchemaVersion right) noexcept
{
    return right.major > left.major || (right.major == left.major && right.minor > left.minor);
}

enum class SemanticClass : std::uint8_t
{
    UserIntent,
    PersistedValue,
    VolatileRuntimeState,
    CapabilityFact,
    DerivedEffectiveValue,
    RestartScopedRequest,
    LegacyAlias,
    UnknownFutureValue,
};

enum class ValueState : std::uint8_t
{
    Missing,
    AutoUnset,
    Explicit,
    Malformed,
};

enum class ApplyScope : std::uint8_t
{
    LiveInput,
    OwnerManagedRebuild,
    RouteTransition,
    RestartProcessCandidate,
    Unresolved,
};

enum class ValueOrigin : std::uint8_t
{
    SchemaDefault,
    Explicit,
    LegacyAlias,
};

enum class MultipassIntent : std::uint8_t
{
    Disabled,
    LegacySerial,
};

enum class AnalysisStatus : std::uint8_t
{
    Equivalent,
    Malformed,
    UnsupportedFutureSchema,
};

enum class MigrationDisposition : std::uint8_t
{
    NoRewriteRequired,
    RefuseMalformed,
    RefuseUnsupportedFutureSchema,
    RefuseNonRepresentable,
};

enum class TransactionState : std::uint8_t
{
    OriginalAuthoritative,
    BackupConfirmed,
    Staged,
    Validated,
    Committed,
    CommitOriginalRetained,
    CommitIndeterminate,
    RolledBack,
    RollbackFailed,
    Failed,
};

enum class CommitOutcome : std::uint8_t
{
    Succeeded,
    OriginalRetained,
    Indeterminate,
};

enum class AuthorityState : std::uint8_t
{
    Original,
    Proposed,
    Unknown,
};

struct RawEntry
{
    std::string section;
    std::string key;
    std::string value;
    std::size_t line = 0;
    std::size_t ordinal = 0;
    bool operator==(const RawEntry&) const = default;
};

struct Document
{
    std::string raw;
    std::vector<RawEntry> entries;
    bool operator==(const Document&) const = default;
};

struct ClassifiedValue
{
    std::string section;
    std::string key;
    std::string rawValue;
    std::size_t line = 0;
    std::size_t ordinal = 0;
    SemanticClass semantic = SemanticClass::UnknownFutureValue;
    ValueState state = ValueState::Explicit;
    ApplyScope scope = ApplyScope::Unresolved;
    bool operator==(const ClassifiedValue&) const = default;
};

struct AlphaCompatibilityIntent
{
    std::optional<std::uint32_t> explicitRoute;
    std::uint32_t route = 2;
    ValueOrigin routeOrigin = ValueOrigin::SchemaDefault;

    int renderingMode = 1;
    ValueOrigin renderingModeOrigin = ValueOrigin::SchemaDefault;

    bool multipassEnabled = false;
    std::uint32_t passes = 1;
    MultipassIntent multipass = MultipassIntent::Disabled;

    std::uint32_t whitePointSource = 1;
    ValueOrigin whitePointOrigin = ValueOrigin::SchemaDefault;

    bool parserAutoObserved = false;
    bool operator==(const AlphaCompatibilityIntent&) const = default;
};

struct IntentSnapshot
{
    SchemaVersion sourceSchema {};
    std::string sourceIdentity;
    std::uint64_t revision = 0;
    std::uint64_t contentFingerprint = 0;
    std::uint64_t snapshotIdentity = 0;
    std::string raw;
    std::vector<ClassifiedValue> values;
    AlphaCompatibilityIntent alpha;
    AnalysisStatus status = AnalysisStatus::Equivalent;

    const ClassifiedValue* FindLast(std::string_view section, std::string_view key) const;
    std::size_t Count(SemanticClass semantic) const;
    bool operator==(const IntentSnapshot&) const = default;
};

struct MigrationPlan
{
    MigrationDisposition disposition = MigrationDisposition::NoRewriteRequired;
    SchemaVersion sourceSchema {};
    SchemaVersion targetSchema {};
    std::string sourceIdentity;
    std::string originalRaw;
    std::optional<std::string> proposedRaw;
    bool preserveOriginal = true;
    bool operator==(const MigrationPlan&) const = default;
};

namespace Detail
{
constexpr char Lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline std::string LowerCopy(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char c : value) result.push_back(Lower(c));
    return result;
}

inline bool EqualNoCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (Lower(left[i]) != Lower(right[i])) return false;
    return true;
}

inline std::string Trim(std::string_view value)
{
    std::size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' ||
                                    value[begin] == '\r' || value[begin] == '\n'))
        ++begin;
    std::size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' || value[end - 1] == '\n'))
        --end;
    return std::string(value.substr(begin, end - begin));
}

inline bool IsOneOf(std::string_view value, std::initializer_list<std::string_view> choices)
{
    for (const auto choice : choices)
        if (EqualNoCase(value, choice)) return true;
    return false;
}

inline bool IsAuto(std::string_view value) { return EqualNoCase(Trim(value), "auto"); }

inline std::optional<bool> ParseBool(std::string_view value)
{
    const auto cleaned = Trim(value);
    if (EqualNoCase(cleaned, "true")) return true;
    if (EqualNoCase(cleaned, "false")) return false;
    return {};
}

// Alpha Config::readInt uses signed int conversion even for readUInt: only an
// unsigned 0x/0X prefix selects hex, and the entire token must be consumed.
// ParseLossless already separates INI whitespace from the preserved raw bytes.
inline std::optional<std::int64_t> ParseSigned(std::string_view value)
{
    const auto cleaned = Trim(value);
    if (cleaned.empty()) return {};
    const bool hexadecimal = cleaned.size() > 2 && cleaned[0] == '0' &&
                             (cleaned[1] == 'x' || cleaned[1] == 'X');
    try
    {
        std::size_t consumed = 0;
        const int result = std::stoi(cleaned, &consumed, hexadecimal ? 16 : 10);
        if (consumed == cleaned.size()) return result;
    }
    catch (const std::invalid_argument&) { }
    catch (const std::out_of_range&) { }
    return {};
}

inline std::optional<std::uint64_t> ParseUnsigned(std::string_view value)
{
    const auto parsed = ParseSigned(value);
    if (!parsed) return {};
    // Match Alpha readUInt's int-to-uint32_t conversion before per-key clamping;
    // do not accept positive values outside the original signed-int range.
    return static_cast<std::uint32_t>(*parsed);
}

inline bool ParseFiniteNumber(std::string_view value)
{
    const auto cleaned = Trim(value);
    if (cleaned.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(cleaned.c_str(), &end);
    return end == cleaned.c_str() + cleaned.size() && std::isfinite(parsed);
}

inline std::uint64_t HashBytes(std::uint64_t hash, std::string_view value) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

inline std::uint64_t HashScalar(std::uint64_t hash, std::uint64_t value) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (unsigned int i = 0; i < 8; ++i)
    {
        hash ^= static_cast<unsigned char>((value >> (i * 8)) & 0xffu);
        hash *= prime;
    }
    return hash;
}

enum class ExpectedKind : std::uint8_t
{
    Any,
    Boolean,
    Unsigned,
    Signed,
    Number,
    FgInput,
    FgOutput,
    FgReplacement,
};

struct Classification
{
    SemanticClass semantic = SemanticClass::UnknownFutureValue;
    ApplyScope scope = ApplyScope::Unresolved;
    ExpectedKind expected = ExpectedKind::Any;
};

inline bool IsLayerSection(std::string_view section)
{
    const auto lower = LowerCopy(section);
    if (lower == "dlssnrlayer2") return true;
    constexpr std::string_view prefix = "dlssnrlayer";
    if (lower.size() <= prefix.size() || lower.substr(0, prefix.size()) != prefix) return false;
    for (std::size_t i = prefix.size(); i < lower.size(); ++i)
        if (lower[i] < '0' || lower[i] > '9') return false;
    return true;
}

inline Classification ClassifyKey(std::string_view section, std::string_view key)
{
    if (EqualNoCase(section, "DlssNr"))
    {
        if (IsOneOf(key, {"PerformanceMode", "RunBeforeSR"}))
            return {SemanticClass::LegacyAlias, ApplyScope::RouteTransition, ExpectedKind::Boolean};
        if (EqualNoCase(key, "WhitePointFromExposure"))
            return {SemanticClass::LegacyAlias, ApplyScope::OwnerManagedRebuild, ExpectedKind::Boolean};
        if (EqualNoCase(key, "Route"))
            return {SemanticClass::UserIntent, ApplyScope::RouteTransition, ExpectedKind::Unsigned};
        if (EqualNoCase(key, "RenderingMode"))
            return {SemanticClass::UserIntent, ApplyScope::RouteTransition, ExpectedKind::Signed};
        if (IsOneOf(key, {"Enabled", "MultipassEnabled", "SecondLayer", "ExperimentalMode",
                          "OverrideMultipassGuardrails", "OverrideHdrGuardrails", "OverrideFgGuardrails",
                          "PreDlaa", "PreSrSoftReset", "PreparedDepth", "UseProxy", "ProxyProbe", "ScanExposure",
                          "ScanMeter", "AutoMask", "ApplyModel", "HoldFrame", "CompareSwap", "CompareTags"}))
            return {SemanticClass::UserIntent,
                    IsOneOf(key, {"Enabled", "MultipassEnabled", "SecondLayer", "ExperimentalMode",
                                  "OverrideMultipassGuardrails", "OverrideHdrGuardrails", "OverrideFgGuardrails", "PreparedDepth"})
                        ? ApplyScope::RouteTransition : ApplyScope::LiveInput,
                    ExpectedKind::Boolean};
        if (IsOneOf(key, {"Passes", "WhitePointSource", "Preset", "Style", "Transfer", "ReversibleMode",
                          "DebugView", "Compare", "ScalingDownscaler", "UiAfterMethod"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Unsigned};
        if (IsOneOf(key, {"Intensity", "LocalStructure", "LocalTone", "SkinStructure", "TransferStrength",
                          "ColourStrength", "MaxRatio", "WorkingScale", "WhitePointTrim", "WhitePointScale",
                          "CompareSplit", "CompareZoom", "TagScale", "UiManualScale", "UiPresentManualScale",
                          "UiEnhancedManualScale", "ScanTrim", "ScanAnchorValue", "ScanAnchorWhitePoint"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Number};
        if (IsOneOf(key, {"ScanAnchors"}))
            return {SemanticClass::PersistedValue, ApplyScope::LiveInput, ExpectedKind::Any};
    }

    if (EqualNoCase(section, "DlssNrBasic"))
    {
        if (IsOneOf(key, {"Advanced", "MaximumPasses", "Resolution", "Downscaler", "ModelStrength", "DetailStrength"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Number};
    }

    if (IsLayerSection(section))
    {
        if (IsOneOf(key, {"AutoMask", "ApplyModel"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Boolean};
        if (IsOneOf(key, {"ScalingDownscaler", "Transfer", "Preset", "Style", "ReversibleMode"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Unsigned};
        if (IsOneOf(key, {"WorkingScale", "Intensity", "LocalStructure", "LocalTone", "SkinStructure",
                          "TransferStrength", "ColourStrength", "MaxRatio"}))
            return {SemanticClass::UserIntent, ApplyScope::OwnerManagedRebuild, ExpectedKind::Number};
    }

    if (EqualNoCase(section, "FrameGen"))
    {
        if (EqualNoCase(key, "FGInput"))
            return {SemanticClass::RestartScopedRequest, ApplyScope::RestartProcessCandidate, ExpectedKind::FgInput};
        if (EqualNoCase(key, "FGOutput"))
            return {SemanticClass::RestartScopedRequest, ApplyScope::RestartProcessCandidate, ExpectedKind::FgOutput};
        if (EqualNoCase(key, "FGNvngxReplacement"))
            return {SemanticClass::RestartScopedRequest, ApplyScope::RestartProcessCandidate,
                    ExpectedKind::FgReplacement};
    }

    if (EqualNoCase(section, "NeuroticIntent") &&
        IsOneOf(key, {"SchemaMajor", "SchemaMinor"}))
        return {SemanticClass::PersistedValue, ApplyScope::Unresolved, ExpectedKind::Unsigned};

    return {};
}

inline bool ValidFor(ExpectedKind expected, std::string_view value)
{
    if (IsAuto(value)) return true;
    switch (expected)
    {
    case ExpectedKind::Any: return true;
    case ExpectedKind::Boolean: return ParseBool(value).has_value();
    case ExpectedKind::Unsigned: return ParseUnsigned(value).has_value();
    case ExpectedKind::Signed: return ParseSigned(value).has_value();
    case ExpectedKind::Number: return ParseFiniteNumber(value);
    case ExpectedKind::FgInput:
        return IsOneOf(Trim(value), {"nofg", "upscaler", "nvngxfg", "dlssg", "fsrfg", "fsrfg30", "nukems"});
    case ExpectedKind::FgOutput:
        return IsOneOf(Trim(value), {"nofg", "fsrfg", "xefg", "dlssg"});
    case ExpectedKind::FgReplacement:
        return IsOneOf(Trim(value), {"none", "nukems", "arturs", "ffx", "combo"});
    }
    return false;
}

inline const ClassifiedValue* FindLast(const std::vector<ClassifiedValue>& values,
                                       std::string_view section, std::string_view key)
{
    for (auto it = values.rbegin(); it != values.rend(); ++it)
        if (EqualNoCase(it->section, section) && EqualNoCase(it->key, key)) return &*it;
    return nullptr;
}

inline std::optional<bool> ExplicitBool(const ClassifiedValue* value)
{
    if (!value || value->state != ValueState::Explicit) return {};
    return ParseBool(value->rawValue);
}

inline std::optional<std::uint64_t> ExplicitUnsigned(const ClassifiedValue* value)
{
    if (!value || value->state != ValueState::Explicit) return {};
    return ParseUnsigned(value->rawValue);
}

inline std::optional<std::int64_t> ExplicitSigned(const ClassifiedValue* value)
{
    if (!value || value->state != ValueState::Explicit) return {};
    return ParseSigned(value->rawValue);
}
} // namespace Detail

inline Document ParseLossless(std::string raw)
{
    Document document;
    document.raw = std::move(raw);
    std::string section;
    // SimpleIni consumes a leading UTF-8 signature for parsing; retain raw bytes for rollback.
    std::size_t position = document.raw.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    std::size_t lineNumber = 1;
    std::size_t ordinal = 0;

    while (position < document.raw.size())
    {
        const auto newline = document.raw.find_first_of("\r\n", position);
        const auto end = newline == std::string::npos ? document.raw.size() : newline;
        std::string_view line(document.raw.data() + position, end - position);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto cleaned = Detail::Trim(line);

        if (!cleaned.empty() && cleaned.front() != ';' && cleaned.front() != '#')
        {
            const auto sectionEnd = cleaned.find(']');
            // Alpha's reader ignores text after the first closing section bracket.
            if (cleaned.front() == '[' && sectionEnd != std::string::npos)
                section = Detail::Trim(std::string_view(cleaned).substr(1, sectionEnd - 1));
            else
            {
                const auto equals = cleaned.find('=');
                if (equals != std::string::npos)
                {
                    RawEntry entry;
                    entry.section = section;
                    entry.key = Detail::Trim(std::string_view(cleaned).substr(0, equals));
                    entry.value = Detail::Trim(std::string_view(cleaned).substr(equals + 1));
                    entry.line = lineNumber;
                    entry.ordinal = ordinal++;
                    document.entries.push_back(std::move(entry));
                }
            }
        }

        if (newline == std::string::npos) break;
        position = newline + 1;
        if (document.raw[newline] == '\r' && position < document.raw.size() && document.raw[position] == '\n')
            ++position;
        ++lineNumber;
    }
    return document;
}

inline const ClassifiedValue* IntentSnapshot::FindLast(std::string_view section, std::string_view key) const
{
    return Detail::FindLast(values, section, key);
}

inline std::size_t IntentSnapshot::Count(SemanticClass semantic) const
{
    return static_cast<std::size_t>(std::count_if(values.begin(), values.end(),
        [semantic](const ClassifiedValue& value) { return value.semantic == semantic; }));
}

inline IntentSnapshot Capture(const Document& document, SchemaVersion sourceSchema,
                              std::string sourceIdentity, std::uint64_t revision)
{
    IntentSnapshot snapshot;
    snapshot.sourceSchema = sourceSchema;
    snapshot.sourceIdentity = std::move(sourceIdentity);
    snapshot.revision = revision;
    snapshot.raw = document.raw;
    snapshot.values.reserve(document.entries.size());

    bool malformed = false;
    std::vector<std::string> seenKnownKeys;
    for (const auto& entry : document.entries)
    {
        const auto classification = Detail::ClassifyKey(entry.section, entry.key);
        bool duplicateKnown = false;
        if (classification.semantic != SemanticClass::UnknownFutureValue)
        {
            auto identity = Detail::LowerCopy(entry.section);
            identity.push_back('');
            identity += Detail::LowerCopy(entry.key);
            duplicateKnown = std::find(seenKnownKeys.begin(), seenKnownKeys.end(), identity) != seenKnownKeys.end();
            if (!duplicateKnown) seenKnownKeys.push_back(std::move(identity));
        }
        ClassifiedValue value;
        value.section = entry.section;
        value.key = entry.key;
        value.rawValue = entry.value;
        value.line = entry.line;
        value.ordinal = entry.ordinal;
        value.semantic = classification.semantic;
        value.scope = classification.scope;
        if (duplicateKnown)
        {
            value.state = ValueState::Malformed;
            malformed = true;
        }
        else if (classification.semantic != SemanticClass::UnknownFutureValue && Detail::IsAuto(entry.value))
        {
            value.state = ValueState::AutoUnset;
            snapshot.alpha.parserAutoObserved = true;
        }
        else if (!Detail::ValidFor(classification.expected, entry.value))
        {
            value.state = ValueState::Malformed;
            malformed = true;
        }
        else
            value.state = ValueState::Explicit;
        snapshot.values.push_back(std::move(value));
    }

    if (NewerThan(sourceSchema, CurrentSchema))
        snapshot.status = AnalysisStatus::UnsupportedFutureSchema;
    else if (malformed)
        snapshot.status = AnalysisStatus::Malformed;

    if (snapshot.status != AnalysisStatus::UnsupportedFutureSchema)
    {
    if (const auto* route = snapshot.FindLast("DlssNr", "Route"))
    {
        if (const auto number = Detail::ExplicitUnsigned(route))
        {
            snapshot.alpha.route = static_cast<std::uint32_t>(std::min<std::uint64_t>(*number, 2));
            snapshot.alpha.explicitRoute = snapshot.alpha.route;
            snapshot.alpha.routeOrigin = ValueOrigin::Explicit;
        }
    }

    if (const auto mode = Detail::ExplicitSigned(snapshot.FindLast("DlssNr", "RenderingMode")))
    {
        snapshot.alpha.renderingMode = static_cast<int>(std::clamp<std::int64_t>(*mode, 0, 1));
        snapshot.alpha.renderingModeOrigin = ValueOrigin::Explicit;
    }
    else if (const auto performance = Detail::ExplicitBool(snapshot.FindLast("DlssNr", "PerformanceMode")))
    {
        snapshot.alpha.renderingMode = *performance ? 1 : 0;
        snapshot.alpha.renderingModeOrigin = ValueOrigin::LegacyAlias;
    }
    else if (const auto before = Detail::ExplicitBool(snapshot.FindLast("DlssNr", "RunBeforeSR")))
    {
        snapshot.alpha.renderingMode = *before ? 1 : 0;
        snapshot.alpha.renderingModeOrigin = ValueOrigin::LegacyAlias;
    }

    const auto explicitMultipass = Detail::ExplicitBool(snapshot.FindLast("DlssNr", "MultipassEnabled"));
    const auto secondLayer = Detail::ExplicitBool(snapshot.FindLast("DlssNr", "SecondLayer"));
    if (const auto passCount = Detail::ExplicitUnsigned(snapshot.FindLast("DlssNr", "Passes")))
        snapshot.alpha.passes = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(*passCount, 1, 10));
    if (explicitMultipass.has_value())
        snapshot.alpha.multipassEnabled = *explicitMultipass;
    else if (secondLayer.value_or(false))
    {
        snapshot.alpha.multipassEnabled = true;
        snapshot.alpha.passes = std::max(2u, snapshot.alpha.passes);
    }
    snapshot.alpha.multipass = snapshot.alpha.multipassEnabled
        ? MultipassIntent::LegacySerial : MultipassIntent::Disabled;

    if (const auto whitePoint = Detail::ExplicitUnsigned(snapshot.FindLast("DlssNr", "WhitePointSource")))
    {
        snapshot.alpha.whitePointSource = static_cast<std::uint32_t>(*whitePoint);
        snapshot.alpha.whitePointOrigin = ValueOrigin::Explicit;
    }
    else if (const auto legacy = Detail::ExplicitBool(snapshot.FindLast("DlssNr", "WhitePointFromExposure")))
    {
        snapshot.alpha.whitePointSource = *legacy ? 1u : 0u;
        snapshot.alpha.whitePointOrigin = ValueOrigin::LegacyAlias;
    }
    }

    constexpr std::uint64_t offset = 14695981039346656037ull;
    auto content = Detail::HashBytes(offset, snapshot.sourceIdentity);
    content = Detail::HashScalar(content, sourceSchema.major);
    content = Detail::HashScalar(content, sourceSchema.minor);
    content = Detail::HashBytes(content, snapshot.raw);
    snapshot.contentFingerprint = content;
    snapshot.snapshotIdentity = Detail::HashScalar(content, revision);
    return snapshot;
}

inline MigrationPlan PrepareMigration(const IntentSnapshot& snapshot, SchemaVersion targetSchema)
{
    MigrationPlan plan;
    plan.sourceSchema = snapshot.sourceSchema;
    plan.targetSchema = targetSchema;
    plan.sourceIdentity = snapshot.sourceIdentity;
    plan.originalRaw = snapshot.raw;

    if (snapshot.status == AnalysisStatus::UnsupportedFutureSchema || NewerThan(targetSchema, CurrentSchema))
        plan.disposition = MigrationDisposition::RefuseUnsupportedFutureSchema;
    else if (snapshot.status == AnalysisStatus::Malformed)
        plan.disposition = MigrationDisposition::RefuseMalformed;
    else if (OlderThan(targetSchema, snapshot.sourceSchema) ||
             (OlderThan(targetSchema, CurrentSchema) &&
              snapshot.Count(SemanticClass::UnknownFutureValue) != 0))
        plan.disposition = MigrationDisposition::RefuseNonRepresentable;
    else
    {
        // Alpha syntax is translated into an immutable intent snapshot without rewriting the INI.
        // In particular, parser auto remains unset/default and never becomes future Orchestrator Auto.
        plan.disposition = MigrationDisposition::NoRewriteRequired;
    }
    return plan;
}

class MigrationTransaction
{
  public:
    MigrationTransaction(std::string original, std::string proposed)
        : original_(std::move(original)), proposed_(std::move(proposed)) {}

    bool ConfirmBackup(bool success)
    {
        if (state_ != TransactionState::OriginalAuthoritative) return false;
        if (!success) { state_ = TransactionState::Failed; return false; }
        backupRetained_ = true;
        state_ = TransactionState::BackupConfirmed;
        return true;
    }

    bool Stage(bool success)
    {
        if (state_ != TransactionState::BackupConfirmed) return false;
        if (!success) { state_ = TransactionState::Failed; return false; }
        state_ = TransactionState::Staged;
        return true;
    }

    bool Validate(bool success)
    {
        if (state_ != TransactionState::Staged) return false;
        if (!success) { state_ = TransactionState::Failed; return false; }
        state_ = TransactionState::Validated;
        return true;
    }

    bool Commit(bool success)
    {
        return Commit(success ? CommitOutcome::Succeeded : CommitOutcome::OriginalRetained);
    }

    bool Commit(CommitOutcome outcome)
    {
        if (state_ != TransactionState::Validated) return false;
        switch (outcome)
        {
        case CommitOutcome::Succeeded:
            state_ = TransactionState::Committed;
            authority_ = AuthorityState::Proposed;
            return true;
        case CommitOutcome::OriginalRetained:
            state_ = TransactionState::CommitOriginalRetained;
            authority_ = AuthorityState::Original;
            return false;
        case CommitOutcome::Indeterminate:
            state_ = TransactionState::CommitIndeterminate;
            authority_ = AuthorityState::Unknown;
            return false;
        }
        return false;
    }

    bool Rollback(bool success)
    {
        if (!backupRetained_ || state_ == TransactionState::OriginalAuthoritative ||
            state_ == TransactionState::RolledBack)
            return false;
        if (!success)
        {
            state_ = TransactionState::RollbackFailed;
            return false;
        }
        state_ = TransactionState::RolledBack;
        authority_ = AuthorityState::Original;
        return true;
    }

    TransactionState state() const noexcept { return state_; }
    AuthorityState authority() const noexcept { return authority_; }
    bool backupRetained() const noexcept { return backupRetained_; }
    const std::string& original() const noexcept { return original_; }
    const std::string& proposed() const noexcept { return proposed_; }
    std::optional<std::string_view> authoritative() const noexcept
    {
        if (authority_ == AuthorityState::Original) return std::string_view(original_);
        if (authority_ == AuthorityState::Proposed) return std::string_view(proposed_);
        return {};
    }

  private:
    std::string original_;
    std::string proposed_;
    TransactionState state_ = TransactionState::OriginalAuthoritative;
    AuthorityState authority_ = AuthorityState::Original;
    bool backupRetained_ = false;
};
} // namespace Neurotic::ConfigIntent
