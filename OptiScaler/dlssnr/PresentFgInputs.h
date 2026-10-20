#pragma once
#include <sl.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace DlssNr::PreFg::UiPlanes
{
// These are callback observations, never resource leases or rendering permission.
// No caller descriptor, native pointer, COM reference or pixel data is retained.
enum class Kind : unsigned { Unknown, ExplicitNull, ResourcePresent, InvalidSchema, MissingNative };
struct Plane
{
    Kind kind = Kind::Unknown;
    uint64_t tagVersion = 0, resourceVersion = 0;
    uint32_t lifecycle = UINT32_MAX, state = UINT32_MAX;
    uint32_t left = 0, top = 0, width = 0, height = 0;
    uint32_t optionalWidth = 0, optionalHeight = 0, optionalFormat = 0;
    bool duplicate = false;
};
struct Batch
{
    Plane hudless, color, alpha;
    unsigned mask = 0, unknownSchemas = 0;
    bool Observed() const noexcept { return mask || unknownSchemas; }
};
inline unsigned Bit(sl::BufferType type) noexcept
{
    return type == sl::kBufferTypeHUDLessColor ? 1u :
        type == sl::kBufferTypeUIColorAndAlpha ? 2u :
        type == sl::kBufferTypeUIAlpha ? 4u : 0u;
}
inline Plane& Slot(Batch& batch, unsigned bit) noexcept
{ return bit == 1 ? batch.hudless : bit == 2 ? batch.color : batch.alpha; }
// Snapshot only while the actual foreign callback owns its descriptors. An
// omitted type remains Unknown; only an explicit null declaration removes it.
inline Batch Capture(const sl::ResourceTag* tags, uint32_t count) noexcept
{
    Batch out;
    if (!tags) { out.unknownSchemas = count != 0; return out; }
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        if (tag.structVersion != sl::kStructVersion1 || tag.structType != sl::ResourceTag::s_structType)
        { ++out.unknownSchemas; continue; }
        const auto bit = Bit(tag.type);
        if (!bit) continue;
        Plane value;
        value.tagVersion = tag.structVersion;
        value.lifecycle = static_cast<uint32_t>(tag.lifecycle);
        value.left = tag.extent.left; value.top = tag.extent.top;
        value.width = tag.extent.width; value.height = tag.extent.height;
        if (!tag.resource) value.kind = Kind::ExplicitNull;
        else
        {
            value.resourceVersion = tag.resource->structVersion;
            if (tag.resource->structVersion != sl::kStructVersion1 ||
                tag.resource->structType != sl::Resource::s_structType)
                value.kind = Kind::InvalidSchema;
            else
            {
                value.kind = tag.resource->native ? Kind::ResourcePresent : Kind::MissingNative;
                value.state = tag.resource->state;
                value.optionalWidth = tag.resource->width;
                value.optionalHeight = tag.resource->height;
                value.optionalFormat = tag.resource->nativeFormat;
            }
        }
        value.duplicate = (out.mask & bit) != 0;
        Slot(out, bit) = value;
        out.mask |= bit;
    }
    return out;
}
struct Key
{
    uint64_t frame = 0, provider = 0, epoch = 0, viewport = 0;
    bool operator==(const Key&) const = default;
    explicit operator bool() const noexcept { return frame && provider && epoch && viewport; }
};
struct Snapshot
{
    Key key;
    Plane hudless, color, alpha;
    uint32_t calls = 0;
    uint64_t unknownSchemas = 0;
    // ResourcePresent means a supplied native value was observed in a known
    // descriptor. It does not prove a usable image, state or content lifetime.
    unsigned observed = 0;
    bool legacy = false;
};
class Ledger
{
    static constexpr std::size_t capacity = 16;
    std::array<Snapshot, capacity> records {};
    std::size_t next = 0;
  public:
    // The containing provider registry serializes access. This ledger never
    // supplies admission truth and cannot extend any SDK content lifetime.
    void Observe(Key key, const Batch& batch, bool legacy) noexcept
    {
        if (!key || !batch.Observed()) return;
        Snapshot* record = nullptr;
        for (auto& value : records) if (value.key == key) { record = &value; break; }
        if (!record)
        {
            record = &records[next]; next = (next + 1) % capacity;
            *record = {}; record->key = key;
        }
        if (batch.mask & 1u) record->hudless = batch.hudless;
        if (batch.mask & 2u) record->color = batch.color;
        if (batch.mask & 4u) record->alpha = batch.alpha;
        record->observed |= batch.mask;
        record->unknownSchemas += batch.unknownSchemas;
        record->legacy = legacy;
        if (record->calls != UINT32_MAX) ++record->calls;
    }
    Snapshot Read(uint64_t frame, uint64_t provider, uint64_t epoch, uint64_t viewport) const noexcept
    {
        const Key key {frame, provider, epoch, viewport};
        if (key) for (const auto& record : records) if (record.key == key) return record;
        return {};
    }
    void Reset() noexcept { records = {}; next = 0; }
};
}
