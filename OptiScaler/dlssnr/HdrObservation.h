#pragma once

#include <dxgi1_6.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace DlssNr::HdrObservation
{
enum class ColorClass : std::uint8_t
{
    Unknown,
    Sdr,
    Hdr10,
    ScRgb,
    Hlg,
    WideGamutGamma,
    Other
};

constexpr ColorClass Classify(DXGI_COLOR_SPACE_TYPE colorSpace)
{
    switch (colorSpace)
    {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        return ColorClass::Sdr;
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        return ColorClass::Hdr10;
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return ColorClass::ScRgb;
    case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
        return ColorClass::Hlg;
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
        return ColorClass::WideGamutGamma;
    case DXGI_COLOR_SPACE_CUSTOM:
        return ColorClass::Unknown;
    default:
        return ColorClass::Other;
    }
}

constexpr const char* ColorClassName(ColorClass value)
{
    switch (value)
    {
    case ColorClass::Sdr: return "SDR";
    case ColorClass::Hdr10: return "HDR10/PQ";
    case ColorClass::ScRgb: return "scRGB";
    case ColorClass::Hlg: return "HLG";
    case ColorClass::WideGamutGamma: return "Rec.2020 gamma";
    case ColorClass::Other: return "other";
    default: return "unknown";
    }
}

constexpr bool IsHdr(ColorClass value)
{
    return value == ColorClass::Hdr10 || value == ColorClass::ScRgb || value == ColorClass::Hlg ||
           value == ColorClass::WideGamutGamma;
}

struct Snapshot
{
    bool registered = false;
    bool colorSpaceObserved = false;
    bool transitioning = false;
    std::uint64_t observationSequence = 0;
    std::uint64_t generation = 0;
    std::uint64_t resizeGeneration = 0;
    std::uint64_t metadataGeneration = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_COLOR_SPACE_TYPE requestedColorSpace = DXGI_COLOR_SPACE_CUSTOM;
    HRESULT colorSpaceResult = S_FALSE;
    HRESULT resizeResult = S_FALSE;
    DXGI_HDR_METADATA_TYPE metadataType = DXGI_HDR_METADATA_TYPE_NONE;
    UINT metadataSize = 0;
    std::uint64_t metadataHash = 0;
    DXGI_HDR_METADATA_TYPE requestedMetadataType = DXGI_HDR_METADATA_TYPE_NONE;
    UINT requestedMetadataSize = 0;
    std::uint64_t requestedMetadataHash = 0;
    HRESULT metadataResult = S_FALSE;
};

class Registry
{
  public:
    static Registry& Instance()
    {
        static Registry registry;
        return registry;
    }

    Snapshot Register(const void* swapChain, DXGI_FORMAT format)
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        auto& entry = _chains[swapChain];
        if (entry.owners++ == 0)
        {
            entry.snapshot = {};
            entry.snapshot.registered = true;
            entry.snapshot.format = format;
            entry.snapshot.generation = 1;
            entry.snapshot.observationSequence = NextSequence();
        }
        return entry.snapshot;
    }

    void Unregister(const void* swapChain)
    {
        if (swapChain == nullptr) return;
        std::lock_guard lock(_mutex);
        const auto found = _chains.find(swapChain);
        if (found == _chains.end()) return;
        if (found->second.owners > 1)
            --found->second.owners;
        else
            _chains.erase(found);
    }

    Snapshot RecordColorSpace(const void* swapChain, DXGI_COLOR_SPACE_TYPE requested, HRESULT result)
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        auto& entry = Ensure(swapChain);
        auto& snapshot = entry.snapshot;
        snapshot.requestedColorSpace = requested;
        snapshot.colorSpaceResult = result;
        snapshot.observationSequence = NextSequence();
        if (SUCCEEDED(result))
        {
            if (!snapshot.colorSpaceObserved || snapshot.colorSpace != requested)
                ++snapshot.generation;
            snapshot.colorSpace = requested;
            snapshot.colorSpaceObserved = true;
        }
        return snapshot;
    }

    Snapshot BeginResize(const void* swapChain)
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        auto& snapshot = Ensure(swapChain).snapshot;
        if (!snapshot.transitioning)
            ++snapshot.generation;
        snapshot.transitioning = true;
        snapshot.observationSequence = NextSequence();
        return snapshot;
    }

    Snapshot CompleteResize(const void* swapChain, HRESULT result, DXGI_FORMAT format)
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        auto& snapshot = Ensure(swapChain).snapshot;
        snapshot.resizeResult = result;
        snapshot.transitioning = false;
        snapshot.observationSequence = NextSequence();
        ++snapshot.generation;
        if (SUCCEEDED(result))
        {
            ++snapshot.resizeGeneration;
            if (format != DXGI_FORMAT_UNKNOWN)
                snapshot.format = format;
        }
        return snapshot;
    }

    Snapshot RecordMetadata(const void* swapChain, DXGI_HDR_METADATA_TYPE type, UINT size,
                            const void* metadata, HRESULT result)
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        auto& snapshot = Ensure(swapChain).snapshot;
        snapshot.requestedMetadataType = type;
        snapshot.requestedMetadataSize = size;
        snapshot.metadataResult = result;
        snapshot.requestedMetadataHash = SUCCEEDED(result) ? HashBounded(metadata, size) : 0;
        snapshot.observationSequence = NextSequence();
        if (SUCCEEDED(result))
        {
            snapshot.metadataType = snapshot.requestedMetadataType;
            snapshot.metadataSize = snapshot.requestedMetadataSize;
            snapshot.metadataHash = snapshot.requestedMetadataHash;
            ++snapshot.metadataGeneration;
            ++snapshot.generation;
        }
        return snapshot;
    }

    Snapshot Read(const void* swapChain) const
    {
        if (swapChain == nullptr) return {};
        std::lock_guard lock(_mutex);
        const auto found = _chains.find(swapChain);
        return found == _chains.end() ? Snapshot {} : found->second.snapshot;
    }

  private:
    struct Entry
    {
        Snapshot snapshot;
        std::uint32_t owners = 0;
    };

    Entry& Ensure(const void* swapChain)
    {
        auto& entry = _chains[swapChain];
        if (!entry.snapshot.registered)
        {
            entry.snapshot.registered = true;
            entry.snapshot.generation = 1;
            entry.owners = 1;
        }
        return entry;
    }

    std::uint64_t NextSequence() { return ++_nextSequence; }

    static std::uint64_t HashBounded(const void* data, UINT size)
    {
        if (data == nullptr || size == 0) return 0;
        constexpr std::size_t limit = 256;
        const auto count = std::min<std::size_t>(size, limit);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < count; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    mutable std::mutex _mutex;
    std::unordered_map<const void*, Entry> _chains;
    std::uint64_t _nextSequence = 0;
};
} // namespace DlssNr::HdrObservation
