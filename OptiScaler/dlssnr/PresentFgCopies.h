#pragma once
#include "PresentFgRestoration.h"
#include <sl.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace DlssNr::PreFg::FinalColor
{
inline std::size_t ConstantsV1Bytes()
{
    const sl::Constants layout;
    return static_cast<std::size_t>(reinterpret_cast<const unsigned char*>(&layout.minRelativeLinearDepthObjectSeparation) -
        reinterpret_cast<const unsigned char*>(&layout));
}
// Destination belongs to the caller's actual hook invocation. No returned
// pointer can refer into a moved temporary. The borrowed next chain is used
// only during the same upstream call and is never retained by our bookkeeping.
inline bool CopyResetConstants(const sl::Constants& source, sl::Constants& destination)
{
    if (source.structType != sl::Constants::s_structType ||
        (source.structVersion != sl::kStructVersion1 && source.structVersion != sl::kStructVersion2)) return false;
    const auto bytes = source.structVersion == sl::kStructVersion1 ? ConstantsV1Bytes() : sizeof(sl::Constants);
    std::memcpy(&destination, &source, bytes);
    destination.reset = sl::Boolean::eTrue;
    return true;
}
inline bool ActualConstantsReset(const sl::Constants& source)
{
    // Public SL structures append new version fields. Read the documented V1
    // prefix even for newer versions, but never truncate their unknown tails.
    return source.structType == sl::Constants::s_structType && source.structVersion >= sl::kStructVersion1 &&
        source.reset == sl::Boolean::eTrue;
}
inline bool OptionalPlane(sl::BufferType type)
{
    return type == sl::kBufferTypeHUDLessColor || type == sl::kBufferTypeUIColorAndAlpha || type == sl::kBufferTypeUIAlpha;
}

// A stack-owned call adapter, destroyed immediately after the original API
// returns. Required descriptors/resources remain authentic; every supported
// optional occurrence, including a chained ResourceTag, has only resource=null.
class TagCopy
{
    struct Node { sl::BaseStructure* local = nullptr; unsigned state = 0; };
    OptionalDeclarations declarations_ {};
    bool declarationsKnown_ = true;
    unsigned declaredMask_ = 0;
    std::vector<sl::ResourceTag> roots_;
    std::vector<std::unique_ptr<sl::ResourceTag>> tags_;
    std::vector<std::unique_ptr<sl::PrecisionInfo>> precision_;
    std::unordered_map<const sl::BaseStructure*, Node> nodes_;

    void Clear()
    {
        roots_.clear(); tags_.clear(); precision_.clear(); nodes_.clear();
        declarations_ = {}; declarationsKnown_ = true; declaredMask_ = 0;
    }
    static bool TagSchema(const sl::BaseStructure& source)
    {
        return source.structType == sl::ResourceTag::s_structType && source.structVersion == sl::kStructVersion1;
    }
    Node* Ensure(const sl::BaseStructure* source)
    {
        const auto found = nodes_.find(source);
        if (found != nodes_.end()) return &found->second;
        sl::BaseStructure* local = nullptr;
        if (TagSchema(*source))
        {
            const auto* tag = static_cast<const sl::ResourceTag*>(source);
            auto value = std::make_unique<sl::ResourceTag>(*tag);
            std::memcpy(value.get(), tag, sizeof(*tag));
            if (OptionalPlane(value->type)) { value->resource = nullptr; declarationsKnown_ = false; }
            local = value.get(); tags_.push_back(std::move(value));
        }
        else if (source->structType == sl::PrecisionInfo::s_structType && source->structVersion == sl::kStructVersion1)
        {
            const auto* precision = static_cast<const sl::PrecisionInfo*>(source);
            auto value = std::make_unique<sl::PrecisionInfo>(*precision);
            std::memcpy(value.get(), precision, sizeof(*precision));
            local = value.get(); precision_.push_back(std::move(value));
        }
        else return nullptr;
        return &nodes_.emplace(source, Node {local, 0}).first->second;
    }
    bool Build(const sl::ResourceTag* source, uint32_t count)
    {
        if (!source) return count == 0;
        for (uint32_t i = 0; i < count; ++i) if (!TagSchema(source[i])) return false;
        roots_.assign(source, source + count);
        // Keep exact representation bytes (including required descriptor padding).
        if (count) std::memcpy(roots_.data(), source, sizeof(sl::ResourceTag) * static_cast<std::size_t>(count));
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& tag = source[i];
            const unsigned bit = tag.type == sl::kBufferTypeHUDLessColor ? 1u :
                tag.type == sl::kBufferTypeUIColorAndAlpha ? 2u : tag.type == sl::kBufferTypeUIAlpha ? 4u : 0u;
            if (bit)
            {
                Declaration value = Declaration::Removed;
                if (tag.resource)
                    value = tag.resource->structType == sl::Resource::s_structType &&
                        tag.resource->structVersion >= sl::kStructVersion1 && tag.resource->native ?
                        Declaration::Present : Declaration::Unknown;
                auto& slot = bit == 1 ? declarations_.hudless : bit == 2 ? declarations_.color : declarations_.alpha;
                if ((declaredMask_ & bit) && slot != value) slot = Declaration::Unknown;
                else if (!(declaredMask_ & bit)) slot = value;
                declaredMask_ |= bit;
                roots_[i].resource = nullptr;
            }
            nodes_.emplace(&source[i], Node {&roots_[i], 0});
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            const sl::BaseStructure* current = &source[i];
            std::vector<const sl::BaseStructure*> path;
            while (current)
            {
                auto* node = Ensure(current);
                if (!node || node->state == 1) return false; // Opaque or cyclic: forward original.
                if (node->state == 2) break;
                node->state = 1;
                auto* local = node->local;
                path.push_back(current);
                const auto* next = current->next;
                if (next)
                {
                    const auto* child = Ensure(next);
                    if (!child) return false;
                    local->next = child->local;
                }
                else local->next = nullptr;
                current = next;
            }
            for (const auto* visited : path) nodes_.at(visited).state = 2;
        }
        return true;
    }
public:
    TagCopy() = default;
    TagCopy(const TagCopy&) = delete;
    TagCopy& operator=(const TagCopy&) = delete;
    bool Prepare(const sl::ResourceTag* source, uint32_t count)
    {
        Clear();
        try { if (Build(source, count)) return true; }
        catch (const std::bad_alloc&) { /* Original API retains its own failure behavior. */ }
        catch (const std::length_error&) { /* No application-specific fixed count limit. */ }
        Clear();
        return false;
    }
    const OptionalDeclarations& OriginalDeclarations() const { return declarations_; }
    bool DeclarationsKnown() const { return declarationsKnown_; }
    const sl::ResourceTag* Data() const { return roots_.empty() ? nullptr : roots_.data(); }
    uint32_t Count() const { return static_cast<uint32_t>(roots_.size()); }
};
}
