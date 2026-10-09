#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "Identity.h"

namespace Neurotic::Contracts
{
enum class GraphicsApi : std::uint32_t
{
    D3D11,
    D3D12,
    Vulkan,
    OpenGL,
    Other,
    D3D10
};
template<> struct EnumTraits<GraphicsApi>
{
    inline static constexpr auto Values = std::array {
        std::pair {GraphicsApi::D3D11, std::string_view {"D3D11"}},
        std::pair {GraphicsApi::D3D12, std::string_view {"D3D12"}},
        std::pair {GraphicsApi::Vulkan, std::string_view {"Vulkan"}},
        std::pair {GraphicsApi::OpenGL, std::string_view {"OpenGL"}},
        std::pair {GraphicsApi::Other, std::string_view {"Other"}},
        std::pair {GraphicsApi::D3D10, std::string_view {"D3D10"}}
    };
};

enum class SemanticKind : std::uint32_t
{
    Color,
    Depth,
    Motion,
    Jitter,
    Exposure,
    Camera,
    Mask,
    Continuity,
    RayReconstruction,
    FrameGeneration,
    Other
};
template<> struct EnumTraits<SemanticKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {SemanticKind::Color, std::string_view {"Color"}},
        std::pair {SemanticKind::Depth, std::string_view {"Depth"}},
        std::pair {SemanticKind::Motion, std::string_view {"Motion"}},
        std::pair {SemanticKind::Jitter, std::string_view {"Jitter"}},
        std::pair {SemanticKind::Exposure, std::string_view {"Exposure"}},
        std::pair {SemanticKind::Camera, std::string_view {"Camera"}},
        std::pair {SemanticKind::Mask, std::string_view {"Mask"}},
        std::pair {SemanticKind::Continuity, std::string_view {"Continuity"}},
        std::pair {SemanticKind::RayReconstruction, std::string_view {"RayReconstruction"}},
        std::pair {SemanticKind::FrameGeneration, std::string_view {"FrameGeneration"}},
        std::pair {SemanticKind::Other, std::string_view {"Other"}}
    };
};

enum class MotionUnits : std::uint32_t
{
    Pixels,
    ActiveRasterUV,
    NDC,
    Blocks
};
template<> struct EnumTraits<MotionUnits>
{
    inline static constexpr auto Values = std::array {
        std::pair {MotionUnits::Pixels, std::string_view {"Pixels"}},
        std::pair {MotionUnits::ActiveRasterUV, std::string_view {"ActiveRasterUV"}},
        std::pair {MotionUnits::NDC, std::string_view {"NDC"}},
        std::pair {MotionUnits::Blocks, std::string_view {"Blocks"}}
    };
};

enum class MotionDirection : std::uint32_t
{
    CurrentToPrevious,
    PreviousToCurrent
};
template<> struct EnumTraits<MotionDirection>
{
    inline static constexpr auto Values = std::array {
        std::pair {MotionDirection::CurrentToPrevious, std::string_view {"CurrentToPrevious"}},
        std::pair {MotionDirection::PreviousToCurrent, std::string_view {"PreviousToCurrent"}}
    };
};

enum class RasterOrigin : std::uint32_t
{
    TopLeft,
    BottomLeft
};
template<> struct EnumTraits<RasterOrigin>
{
    inline static constexpr auto Values = std::array {
        std::pair {RasterOrigin::TopLeft, std::string_view {"TopLeft"}},
        std::pair {RasterOrigin::BottomLeft, std::string_view {"BottomLeft"}}
    };
};

enum class JitterConvention : std::uint32_t
{
    Unjittered,
    Jittered
};
template<> struct EnumTraits<JitterConvention>
{
    inline static constexpr auto Values = std::array {
        std::pair {JitterConvention::Unjittered, std::string_view {"Unjittered"}},
        std::pair {JitterConvention::Jittered, std::string_view {"Jittered"}}
    };
};

enum class DepthKind : std::uint32_t
{
    Device,
    PositiveViewAxis,
    RadialDistance,
    InverseDepth,
    Control,
    Proxy
};
template<> struct EnumTraits<DepthKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {DepthKind::Device, std::string_view {"Device"}},
        std::pair {DepthKind::PositiveViewAxis, std::string_view {"PositiveViewAxis"}},
        std::pair {DepthKind::RadialDistance, std::string_view {"RadialDistance"}},
        std::pair {DepthKind::InverseDepth, std::string_view {"InverseDepth"}},
        std::pair {DepthKind::Control, std::string_view {"Control"}},
        std::pair {DepthKind::Proxy, std::string_view {"Proxy"}}
    };
};

enum class ColorDomain : std::uint32_t
{
    SceneLinear,
    DisplayLinear,
    EncodedDisplay,
    Control
};
template<> struct EnumTraits<ColorDomain>
{
    inline static constexpr auto Values = std::array {
        std::pair {ColorDomain::SceneLinear, std::string_view {"SceneLinear"}},
        std::pair {ColorDomain::DisplayLinear, std::string_view {"DisplayLinear"}},
        std::pair {ColorDomain::EncodedDisplay, std::string_view {"EncodedDisplay"}},
        std::pair {ColorDomain::Control, std::string_view {"Control"}}
    };
};

enum class AlphaMeaning : std::uint32_t
{
    Opaque,
    Straight,
    Premultiplied,
    Unspecified
};
template<> struct EnumTraits<AlphaMeaning>
{
    inline static constexpr auto Values = std::array {
        std::pair {AlphaMeaning::Opaque, std::string_view {"Opaque"}},
        std::pair {AlphaMeaning::Straight, std::string_view {"Straight"}},
        std::pair {AlphaMeaning::Premultiplied, std::string_view {"Premultiplied"}},
        std::pair {AlphaMeaning::Unspecified, std::string_view {"Unspecified"}}
    };
};

enum class BoundaryKind : std::uint32_t
{
    Acquisition,
    BeforeUpscale,
    AfterUpscale,
    BeforeToneMap,
    AfterToneMap,
    BeforeHud,
    AfterHud,
    BeforeFg,
    PresentAttempt
};
template<> struct EnumTraits<BoundaryKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {BoundaryKind::Acquisition, std::string_view {"Acquisition"}},
        std::pair {BoundaryKind::BeforeUpscale, std::string_view {"BeforeUpscale"}},
        std::pair {BoundaryKind::AfterUpscale, std::string_view {"AfterUpscale"}},
        std::pair {BoundaryKind::BeforeToneMap, std::string_view {"BeforeToneMap"}},
        std::pair {BoundaryKind::AfterToneMap, std::string_view {"AfterToneMap"}},
        std::pair {BoundaryKind::BeforeHud, std::string_view {"BeforeHud"}},
        std::pair {BoundaryKind::AfterHud, std::string_view {"AfterHud"}},
        std::pair {BoundaryKind::BeforeFg, std::string_view {"BeforeFg"}},
        std::pair {BoundaryKind::PresentAttempt, std::string_view {"PresentAttempt"}}
    };
};

enum class Placement : std::uint32_t
{
    NativeBefore,
    NativeAfter,
    UnifiedPresent
};
template<> struct EnumTraits<Placement>
{
    inline static constexpr auto Values = std::array {
        std::pair {Placement::NativeBefore, std::string_view {"NativeBefore"}},
        std::pair {Placement::NativeAfter, std::string_view {"NativeAfter"}},
        std::pair {Placement::UnifiedPresent, std::string_view {"UnifiedPresent"}}
    };
};

enum class MaskPurpose : std::uint32_t
{
    MotionConfidence,
    LocalHistoryRejection,
    Reactive,
    Transparency,
    UiExclusion,
    NrApplication,
    ModelSpecific,
    MotionValidity,
    Disocclusion
};
template<> struct EnumTraits<MaskPurpose>
{
    inline static constexpr auto Values = std::array {
        std::pair {MaskPurpose::MotionConfidence, std::string_view {"MotionConfidence"}},
        std::pair {MaskPurpose::LocalHistoryRejection, std::string_view {"LocalHistoryRejection"}},
        std::pair {MaskPurpose::Reactive, std::string_view {"Reactive"}},
        std::pair {MaskPurpose::Transparency, std::string_view {"Transparency"}},
        std::pair {MaskPurpose::UiExclusion, std::string_view {"UiExclusion"}},
        std::pair {MaskPurpose::NrApplication, std::string_view {"NrApplication"}},
        std::pair {MaskPurpose::ModelSpecific, std::string_view {"ModelSpecific"}},
        std::pair {MaskPurpose::MotionValidity, std::string_view {"MotionValidity"}},
        std::pair {MaskPurpose::Disocclusion, std::string_view {"Disocclusion"}}
    };
};

enum class TransformKind : std::uint32_t
{
    FormatConversion,
    RasterRemap,
    MotionScale,
    JitterRemoval,
    ExposureUndo,
    ExposureApply,
    ColorTransfer,
    DepthConversion,
    Other
};
template<> struct EnumTraits<TransformKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {TransformKind::FormatConversion, std::string_view {"FormatConversion"}},
        std::pair {TransformKind::RasterRemap, std::string_view {"RasterRemap"}},
        std::pair {TransformKind::MotionScale, std::string_view {"MotionScale"}},
        std::pair {TransformKind::JitterRemoval, std::string_view {"JitterRemoval"}},
        std::pair {TransformKind::ExposureUndo, std::string_view {"ExposureUndo"}},
        std::pair {TransformKind::ExposureApply, std::string_view {"ExposureApply"}},
        std::pair {TransformKind::ColorTransfer, std::string_view {"ColorTransfer"}},
        std::pair {TransformKind::DepthConversion, std::string_view {"DepthConversion"}},
        std::pair {TransformKind::Other, std::string_view {"Other"}}
    };
};

enum class FailureDisposition : std::uint32_t
{
    PreserveOriginal,
    AcceptDeclaredPrefix,
    Bypass,
    Interrupted
};
template<> struct EnumTraits<FailureDisposition>
{
    inline static constexpr auto Values = std::array {
        std::pair {FailureDisposition::PreserveOriginal, std::string_view {"PreserveOriginal"}},
        std::pair {FailureDisposition::AcceptDeclaredPrefix, std::string_view {"AcceptDeclaredPrefix"}},
        std::pair {FailureDisposition::Bypass, std::string_view {"Bypass"}},
        std::pair {FailureDisposition::Interrupted, std::string_view {"Interrupted"}}
    };
};

enum class UsageKind : std::uint32_t
{
    CallbackRead,
    OrderedGpuRead,
    WriteExclusive,
    RetainedProviderConsumption
};
template<> struct EnumTraits<UsageKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {UsageKind::CallbackRead, std::string_view {"CallbackRead"}},
        std::pair {UsageKind::OrderedGpuRead, std::string_view {"OrderedGpuRead"}},
        std::pair {UsageKind::WriteExclusive, std::string_view {"WriteExclusive"}},
        std::pair {UsageKind::RetainedProviderConsumption, std::string_view {"RetainedProviderConsumption"}}
    };
};

enum class ResourceCapability : std::uint32_t
{
    Sampled,
    Storage,
    CopySource,
    CopyDestination
};
template<> struct EnumTraits<ResourceCapability>
{
    inline static constexpr auto Values = std::array {
        std::pair {ResourceCapability::Sampled, std::string_view {"Sampled"}},
        std::pair {ResourceCapability::Storage, std::string_view {"Storage"}},
        std::pair {ResourceCapability::CopySource, std::string_view {"CopySource"}},
        std::pair {ResourceCapability::CopyDestination, std::string_view {"CopyDestination"}}
    };
};

enum class ClockDomain : std::uint32_t
{
    CpuMonotonic,
    GpuQueue,
    Provider,
    External
};
template<> struct EnumTraits<ClockDomain>
{
    inline static constexpr auto Values = std::array {
        std::pair {ClockDomain::CpuMonotonic, std::string_view {"CpuMonotonic"}},
        std::pair {ClockDomain::GpuQueue, std::string_view {"GpuQueue"}},
        std::pair {ClockDomain::Provider, std::string_view {"Provider"}},
        std::pair {ClockDomain::External, std::string_view {"External"}}
    };
};

enum class MeasurementUnit : std::uint32_t
{
    Nanoseconds,
    Bytes,
    Count
};
template<> struct EnumTraits<MeasurementUnit>
{
    inline static constexpr auto Values = std::array {
        std::pair {MeasurementUnit::Nanoseconds, std::string_view {"Nanoseconds"}},
        std::pair {MeasurementUnit::Bytes, std::string_view {"Bytes"}},
        std::pair {MeasurementUnit::Count, std::string_view {"Count"}}
    };
};

struct Vec2
{
    double x {};
    double y {};
    inline static constexpr std::string_view WireName = "Vec2";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("x", &Vec2::x),
            Field("y", &Vec2::y)
        };
    }
    bool operator==(const Vec2&) const = default;
};

// Empty rectangles are expressible. Executable image coverage is qualified by NFC, not by zero coercion.
struct Rectangle
{
    std::uint32_t x {};
    std::uint32_t y {};
    std::uint32_t width {};
    std::uint32_t height {};
    inline static constexpr std::string_view WireName = "Rectangle";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("x", &Rectangle::x),
            Field("y", &Rectangle::y),
            Field("width", &Rectangle::width),
            Field("height", &Rectangle::height)
        };
    }
    bool operator==(const Rectangle&) const = default;
};

struct Extent
{
    std::uint32_t width {};
    std::uint32_t height {};
    inline static constexpr std::string_view WireName = "Extent";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("width", &Extent::width),
            Field("height", &Extent::height)
        };
    }
    bool operator==(const Extent&) const = default;
};

struct RasterDescription
{
    OptionalFact<Extent> allocation {};
    OptionalFact<Rectangle> active {};
    OptionalFact<RasterOrigin> origin {};
    OptionalFact<Vec2> pixelCenter {};
    OptionalFact<Vec2> axisSigns {};
    OptionalFact<ContractRef<ContractId::C14>> samplingIdentity {};
    inline static constexpr std::string_view WireName = "RasterDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("allocation", &RasterDescription::allocation),
            Field("active", &RasterDescription::active),
            Field("origin", &RasterDescription::origin),
            Field("pixelCenter", &RasterDescription::pixelCenter),
            Field("axisSigns", &RasterDescription::axisSigns),
            Field("samplingIdentity", &RasterDescription::samplingIdentity)
        };
    }
    Error Check() const
    {
        if (allocation.IsKnown() && active.IsKnown())
        {
            const auto size = allocation.KnownPart()->value;
            const auto rect = active.KnownPart()->value;
            if (rect.x > size.width || rect.y > size.height || rect.width > size.width - rect.x ||
                rect.height > size.height - rect.y)
                return Error::Malformed;
        }
        return Error::None;
    }
    bool operator==(const RasterDescription&) const = default;
};

struct FramePair
{
    OptionalFact<BaseRealFrameId> current {};
    OptionalFact<BaseRealFrameId> previous {};
    BoundedList<EvidenceRef, 8> associationEvidence {};
    inline static constexpr std::string_view WireName = "FramePair";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("current", &FramePair::current),
            Field("previous", &FramePair::previous),
            Field("associationEvidence", &FramePair::associationEvidence)
        };
    }
    bool operator==(const FramePair&) const = default;
};

struct ColorDescription
{
    OptionalFact<ColorDomain> domain {};
    OptionalFact<Symbol> primaries {};
    OptionalFact<Symbol> transfer {};
    OptionalFact<Symbol> range {};
    OptionalFact<AlphaMeaning> alpha {};
    OptionalFact<double> paperWhiteNits {};
    OptionalFact<double> peakNits {};
    inline static constexpr std::string_view WireName = "ColorDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("domain", &ColorDescription::domain),
            Field("primaries", &ColorDescription::primaries),
            Field("transfer", &ColorDescription::transfer),
            Field("range", &ColorDescription::range),
            Field("alpha", &ColorDescription::alpha),
            Field("paperWhiteNits", &ColorDescription::paperWhiteNits),
            Field("peakNits", &ColorDescription::peakNits)
        };
    }
    bool operator==(const ColorDescription&) const = default;
};

struct DepthDescription
{
    OptionalFact<DepthKind> kind {};
    OptionalFact<bool> reversed {};
    OptionalFact<Symbol> projectionConvention {};
    OptionalFact<double> nearPlane {};
    OptionalFact<double> farPlane {};
    OptionalFact<BoundedList<double, 16>> projection {};
    inline static constexpr std::string_view WireName = "DepthDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("kind", &DepthDescription::kind),
            Field("reversed", &DepthDescription::reversed),
            Field("projectionConvention", &DepthDescription::projectionConvention),
            Field("nearPlane", &DepthDescription::nearPlane),
            Field("farPlane", &DepthDescription::farPlane),
            Field("projection", &DepthDescription::projection)
        };
    }
    bool operator==(const DepthDescription&) const = default;
};

struct MotionDescription
{
    OptionalFact<MotionUnits> units {};
    OptionalFact<MotionDirection> direction {};
    OptionalFact<JitterConvention> jitterConvention {};
    OptionalFact<Vec2> scale {};
    OptionalFact<Vec2> currentJitter {};
    OptionalFact<Vec2> previousJitter {};
    FramePair framePair {};
    // Raw Native guide association; it supplies no real-frame pair semantics.
    std::optional<MetadataRef<NativeSampleIdentityV1>> nativeSample;
    RasterDescription currentRaster {};
    RasterDescription previousRaster {};
    OptionalFact<Rectangle> validCoverage {};
    inline static constexpr std::string_view WireName = "MotionDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("units", &MotionDescription::units),
            Field("direction", &MotionDescription::direction),
            Field("jitterConvention", &MotionDescription::jitterConvention),
            Field("scale", &MotionDescription::scale),
            Field("currentJitter", &MotionDescription::currentJitter),
            Field("previousJitter", &MotionDescription::previousJitter),
            Field("framePair", &MotionDescription::framePair),
            Field("nativeSample", &MotionDescription::nativeSample),
            Field("currentRaster", &MotionDescription::currentRaster),
            Field("previousRaster", &MotionDescription::previousRaster),
            Field("validCoverage", &MotionDescription::validCoverage)
        };
    }
    bool operator==(const MotionDescription&) const = default;
};

struct ExposureDescription
{
    OptionalFact<double> rawExposure {};
    OptionalFact<double> effectiveExposure {};
    OptionalFact<double> preExposure {};
    OptionalFact<Symbol> preExposureRelation {};
    OptionalFact<double> calibration {};
    OptionalFact<OwnerValueReference> overrideLineage {};
    inline static constexpr std::string_view WireName = "ExposureDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("rawExposure", &ExposureDescription::rawExposure),
            Field("effectiveExposure", &ExposureDescription::effectiveExposure),
            Field("preExposure", &ExposureDescription::preExposure),
            Field("preExposureRelation", &ExposureDescription::preExposureRelation),
            Field("calibration", &ExposureDescription::calibration),
            Field("overrideLineage", &ExposureDescription::overrideLineage)
        };
    }
    bool operator==(const ExposureDescription&) const = default;
};

struct ResourceDescriptor
{
    OptionalFact<Extent> allocation {};
    OptionalFact<Symbol> format {};
    OptionalFact<std::uint32_t> sampleCount {};
    OptionalFact<std::uint32_t> mipLevels {};
    OptionalFact<std::uint32_t> arrayLayers {};
    OptionalFact<GraphicsApi> api {};
    inline static constexpr std::string_view WireName = "ResourceDescriptor";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("allocation", &ResourceDescriptor::allocation),
            Field("format", &ResourceDescriptor::format),
            Field("sampleCount", &ResourceDescriptor::sampleCount),
            Field("mipLevels", &ResourceDescriptor::mipLevels),
            Field("arrayLayers", &ResourceDescriptor::arrayLayers),
            Field("api", &ResourceDescriptor::api)
        };
    }
    Error Check() const
    {
        if ((sampleCount.IsKnown() && sampleCount.KnownPart()->value == 0) ||
            (mipLevels.IsKnown() && mipLevels.KnownPart()->value == 0) ||
            (arrayLayers.IsKnown() && arrayLayers.KnownPart()->value == 0))
            return Error::Malformed;
        return Error::None;
    }
    bool operator==(const ResourceDescriptor&) const = default;
};

// No pointer, COM object, native fence or ownership-bearing object is stored.
struct ResourceView
{
    ResourceIdentityToken identity {};
    ResourceDescriptor descriptor {};
    OptionalFact<ObjectIncarnation> device {};
    OptionalFact<Symbol> adapter {};
    OptionalFact<std::uint32_t> mip {};
    OptionalFact<std::uint32_t> arrayLayer {};
    OptionalFact<std::uint32_t> plane {};
    RasterDescription raster {};
    inline static constexpr std::string_view WireName = "ResourceView";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("identity", &ResourceView::identity),
            Field("descriptor", &ResourceView::descriptor),
            Field("device", &ResourceView::device),
            Field("adapter", &ResourceView::adapter),
            Field("mip", &ResourceView::mip),
            Field("arrayLayer", &ResourceView::arrayLayer),
            Field("plane", &ResourceView::plane),
            Field("raster", &ResourceView::raster)
        };
    }
    Error Check() const
    {
        const auto* mipCount = descriptor.mipLevels.KnownPart();
        const auto* selectedMip = mip.KnownPart();
        if (mipCount && selectedMip && selectedMip->value >= mipCount->value)
            return Error::Malformed;
        const auto* layerCount = descriptor.arrayLayers.KnownPart();
        const auto* selectedLayer = arrayLayer.KnownPart();
        if (layerCount && selectedLayer && selectedLayer->value >= layerCount->value)
            return Error::Malformed;
        return Error::None;
    }
    bool operator==(const ResourceView&) const = default;
};

struct ProfileKey
{
    Symbol consumer {};
    Symbol profile {};
    std::uint32_t version = 1;
    inline static constexpr std::string_view WireName = "ProfileKey";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("consumer", &ProfileKey::consumer),
            Field("profile", &ProfileKey::profile),
            Field("version", &ProfileKey::version)
        };
    }
    Error Check() const
    {
        return consumer.Empty() || profile.Empty() || version == 0 ? Error::MissingField : Error::None;
    }
    bool operator==(const ProfileKey&) const = default;
};

struct BoundaryDescription
{
    OptionalFact<BoundaryKind> kind {};
    OptionalFact<EpisodeId> episode {};
    OptionalFact<bool> afterRequiredHostRendering {};
    OptionalFact<bool> hudIncluded {};
    OptionalFact<bool> toneMapped {};
    inline static constexpr std::string_view WireName = "BoundaryDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("kind", &BoundaryDescription::kind),
            Field("episode", &BoundaryDescription::episode),
            Field("afterRequiredHostRendering", &BoundaryDescription::afterRequiredHostRendering),
            Field("hudIncluded", &BoundaryDescription::hudIncluded),
            Field("toneMapped", &BoundaryDescription::toneMapped)
        };
    }
    bool operator==(const BoundaryDescription&) const = default;
};

// Required rights only. This descriptor neither contains nor grants an acquired C03 lease.
struct AccessRequirements
{
    BoundedList<UsageKind, 4> uses {};
    OptionalFact<ObjectIncarnation> device {};
    OptionalFact<ObjectIncarnation> queue {};
    OptionalFact<RecordKey> callbackScope {};
    OptionalFact<bool> requiresExactContent {};
    OptionalFact<Symbol> retirementContract {};
    inline static constexpr std::string_view WireName = "AccessRequirements";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("uses", &AccessRequirements::uses),
            Field("device", &AccessRequirements::device),
            Field("queue", &AccessRequirements::queue),
            Field("callbackScope", &AccessRequirements::callbackScope),
            Field("requiresExactContent", &AccessRequirements::requiresExactContent),
            Field("retirementContract", &AccessRequirements::retirementContract)
        };
    }
    bool operator==(const AccessRequirements&) const = default;
};

struct Measurement
{
    OptionalFact<double> value {};
    MeasurementUnit unit = MeasurementUnit::Nanoseconds;
    OptionalFact<ClockDomain> clock {};
    OptionalFact<RecordKey> clockInstance {};
    OptionalFact<EvidenceRef> measurementEvidence {};
    inline static constexpr std::string_view WireName = "Measurement";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("value", &Measurement::value),
            Field("unit", &Measurement::unit),
            Field("clock", &Measurement::clock),
            Field("clockInstance", &Measurement::clockInstance),
            Field("measurementEvidence", &Measurement::measurementEvidence)
        };
    }
    bool operator==(const Measurement&) const = default;
};

struct CostEstimate
{
    Measurement latency {};
    Measurement incrementalMemory {};
    OptionalFact<Symbol> method {};
    inline static constexpr std::string_view WireName = "CostEstimate";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("latency", &CostEstimate::latency),
            Field("incrementalMemory", &CostEstimate::incrementalMemory),
            Field("method", &CostEstimate::method)
        };
    }
    bool operator==(const CostEstimate&) const = default;
};

struct RasterMapping
{
    RasterDescription source {};
    RasterDescription target {};
    OptionalFact<Vec2> scale {};
    OptionalFact<Vec2> offset {};
    OptionalFact<RecordReference> mappingEvidence {};
    inline static constexpr std::string_view WireName = "RasterMapping";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("source", &RasterMapping::source),
            Field("target", &RasterMapping::target),
            Field("scale", &RasterMapping::scale),
            Field("offset", &RasterMapping::offset),
            Field("mappingEvidence", &RasterMapping::mappingEvidence)
        };
    }
    bool operator==(const RasterMapping&) const = default;
};

struct TransformStep
{
    TransformKind kind = TransformKind::FormatConversion;
    Symbol operation {};
    std::uint32_t version = 1;
    OptionalFact<Symbol> sourceUnits {};
    OptionalFact<Symbol> targetUnits {};
    MetadataList<SemanticClaim, 8> parameters {};
    OptionalFact<RecordReference> qualification {};
    OptionalFact<bool> alreadyApplied {};
    inline static constexpr std::string_view WireName = "TransformStep";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("kind", &TransformStep::kind),
            Field("operation", &TransformStep::operation),
            Field("version", &TransformStep::version),
            Field("sourceUnits", &TransformStep::sourceUnits),
            Field("targetUnits", &TransformStep::targetUnits),
            Field("parameters", &TransformStep::parameters),
            Field("qualification", &TransformStep::qualification),
            Field("alreadyApplied", &TransformStep::alreadyApplied)
        };
    }
    bool operator==(const TransformStep&) const = default;
};

// ContentRevision is intentionally not a field. Builders, comparison policy and hashing remain downstream.
struct StructuralSignature
{
    Symbol schema {};
    std::uint32_t version = 1;
    MetadataList<RecordKey, 32> structuralDependencies {};
    MetadataRef<GenerationVector> generations {};
    inline static constexpr std::string_view WireName = "StructuralSignature";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("schema", &StructuralSignature::schema),
            Field("version", &StructuralSignature::version),
            Field("structuralDependencies", &StructuralSignature::structuralDependencies),
            Field("generations", &StructuralSignature::generations)
        };
    }
    bool operator==(const StructuralSignature&) const = default;
};

struct HistoryKey
{
    RecordKey owner {};
    Symbol slot {};
    MetadataRef<StructuralSignature> dependencies {};
    OptionalFact<HistoryGeneration> generation {};
    inline static constexpr std::string_view WireName = "HistoryKey";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("owner", &HistoryKey::owner),
            Field("slot", &HistoryKey::slot),
            Field("dependencies", &HistoryKey::dependencies),
            Field("generation", &HistoryKey::generation)
        };
    }
    bool operator==(const HistoryKey&) const = default;
};

template<MaskPurpose P> struct TypedMask
{
    RecordReference source;
    OptionalFact<Rectangle> coverage;
    OptionalFact<RecordReference> qualifiedMapping;
    inline static constexpr MaskPurpose Purpose = P;
    static constexpr auto Fields()
    {
        return std::tuple {Field("source", &TypedMask::source), Field("coverage", &TypedMask::coverage),
                           Field("qualifiedMapping", &TypedMask::qualifiedMapping)};
    }
    bool operator==(const TypedMask&) const = default;
};
struct MaskSet
{
    OptionalFact<TypedMask<MaskPurpose::MotionConfidence>> motionConfidence {};
    OptionalFact<TypedMask<MaskPurpose::LocalHistoryRejection>> localHistoryRejection {};
    OptionalFact<TypedMask<MaskPurpose::Reactive>> reactive {};
    OptionalFact<TypedMask<MaskPurpose::Transparency>> transparency {};
    OptionalFact<TypedMask<MaskPurpose::UiExclusion>> uiExclusion {};
    OptionalFact<TypedMask<MaskPurpose::NrApplication>> nrApplication {};
    OptionalFact<TypedMask<MaskPurpose::ModelSpecific>> modelSpecific {};
    OptionalFact<TypedMask<MaskPurpose::MotionValidity>> motionValidity {};
    OptionalFact<TypedMask<MaskPurpose::Disocclusion>> disocclusion {};
    inline static constexpr std::string_view WireName = "MaskSet";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("motionConfidence", &MaskSet::motionConfidence),
            Field("localHistoryRejection", &MaskSet::localHistoryRejection),
            Field("reactive", &MaskSet::reactive),
            Field("transparency", &MaskSet::transparency),
            Field("uiExclusion", &MaskSet::uiExclusion),
            Field("nrApplication", &MaskSet::nrApplication),
            Field("modelSpecific", &MaskSet::modelSpecific),
            Field("motionValidity", &MaskSet::motionValidity),
            Field("disocclusion", &MaskSet::disocclusion)
        };
    }
    bool operator==(const MaskSet&) const = default;
};

struct GuideDescription
{
    OptionalFact<ContractRef<ContractId::C01>> candidate {};
    SemanticKind kind = SemanticKind::Depth;
    RasterDescription raster {};
    EvidenceVector evidence {};
    OptionalFact<RecordReference> semanticDescription {};
    inline static constexpr std::string_view WireName = "GuideDescription";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("candidate", &GuideDescription::candidate),
            Field("kind", &GuideDescription::kind),
            Field("raster", &GuideDescription::raster),
            Field("evidence", &GuideDescription::evidence),
            Field("semanticDescription", &GuideDescription::semanticDescription)
        };
    }
    bool operator==(const GuideDescription&) const = default;
};

} // namespace Neurotic::Contracts
