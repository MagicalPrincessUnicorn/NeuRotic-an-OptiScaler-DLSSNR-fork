// GPL-3.0. Actual worker color domain; no provider/device identity policy.
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace nrw::color {
enum class Storage {Unknown,Bgra8,Rgba16Float};
enum class Domain {Unknown,Srgb709,LinearScRgb709};
enum class Primaries {Unknown,Rec709};
enum class Units {Unknown,EncodedRelative,ScRgb80Nits};
// WGC's documented color domain does not establish an alpha convention.
// Unspecified channels must be preserved without premultiply/unpremultiply.
// The destination's actual alpha configuration is recorded separately.
enum class Alpha {Unspecified,Opaque,Straight,Premultiplied};
enum class Provenance {Unknown,DeclaredSdrProducer,WgcComposed,GeneratedSdrPreview};
struct Representation {
    Storage storage=Storage::Unknown;Domain domain=Domain::Unknown;
    Primaries primaries=Primaries::Unknown;Units units=Units::Unknown;
    Alpha alpha=Alpha::Unspecified;Provenance provenance=Provenance::Unknown;
    std::uint64_t epoch=0;std::uint32_t recipe=0;
    bool operator==(const Representation&)const=default;
};
struct ModelRecipe {float whitePoint=1;std::uint32_t passthrough=1,signedOriginal=0,revision=0;};
// Compatibility for existing explicit internal SDR producers/fixtures. Product
// capture replaces this declaration with the negotiated WGC representation.
inline constexpr Representation DeclaredSdr(){
    return {Storage::Bgra8,Domain::Srgb709,Primaries::Rec709,Units::EncodedRelative,
        Alpha::Unspecified,Provenance::DeclaredSdrProducer,1,0};
}
inline constexpr Representation WgcSdr(std::uint64_t epoch){
    auto result=DeclaredSdr();result.provenance=Provenance::WgcComposed;result.epoch=epoch;return result;
}
inline constexpr Representation WgcScRgb(std::uint64_t epoch){
    return {Storage::Rgba16Float,Domain::LinearScRgb709,Primaries::Rec709,Units::ScRgb80Nits,
        Alpha::Unspecified,Provenance::WgcComposed,epoch,1};
}
inline constexpr bool Valid(const Representation& value){
    if(!value.epoch||value.primaries!=Primaries::Rec709)return false;
    switch(value.alpha){case Alpha::Unspecified:case Alpha::Opaque:case Alpha::Straight:case Alpha::Premultiplied:break;default:return false;}
    if(value.domain==Domain::Srgb709)
        return value.storage==Storage::Bgra8&&value.units==Units::EncodedRelative&&value.recipe==0&&
            (value.provenance==Provenance::DeclaredSdrProducer||value.provenance==Provenance::WgcComposed||(value.provenance==Provenance::GeneratedSdrPreview&&value.alpha==Alpha::Opaque));
    if(value.domain==Domain::LinearScRgb709)
        return value.storage==Storage::Rgba16Float&&value.units==Units::ScRgb80Nits&&value.recipe==1&&
            (value.provenance==Provenance::WgcComposed||(value.provenance==Provenance::GeneratedSdrPreview&&value.alpha==Alpha::Opaque));
    return false;
}
inline constexpr bool Matches(const Representation& value,Storage actual,std::uint64_t epoch){
    return Valid(value)&&value.storage==actual&&value.epoch==epoch;
}
inline constexpr bool SameDomain(const Representation& a,const Representation& b){return Valid(a)&&Valid(b)&&a==b;}
inline constexpr Representation PreviewFor(Representation source){
    source.alpha=Alpha::Opaque;source.provenance=Provenance::GeneratedSdrPreview;return source;
}
inline constexpr bool PairCompatible(const Representation& source,const Representation& processed){
    if(SameDomain(source,processed))return true;
    return Valid(source)&&Valid(processed)&&processed.provenance==Provenance::GeneratedSdrPreview&&processed.alpha==Alpha::Opaque&&
        source.storage==processed.storage&&source.domain==processed.domain&&source.primaries==processed.primaries&&source.units==processed.units&&source.epoch==processed.epoch&&source.recipe==processed.recipe;
}
inline constexpr bool Hdr(const Representation& value){return Valid(value)&&value.domain==Domain::LinearScRgb709;}
inline constexpr std::optional<ModelRecipe> Model(const Representation& value){
    if(!Valid(value))return {};
    // Existing PresentColor HDR recipe: source is composed linear709 scRGB,
    // normalized for the model proxy; signed original remains independently
    // retained. This is not monitor SDR-white scaling or game exposure.
    return Hdr(value)?ModelRecipe{2.5f,0,1,1}:ModelRecipe{1.f,1,0,0};
}
inline constexpr std::optional<std::uint64_t> FrameBytes(std::uint32_t width,std::uint32_t height,Storage storage){
    const std::uint64_t bpp=storage==Storage::Bgra8?4:storage==Storage::Rgba16Float?8:0;
    if(!width||!height||!bpp)return {};
    const auto pixels=std::uint64_t(width)*height;
    if(pixels>(std::numeric_limits<std::uint64_t>::max)()/bpp)return {};
    return pixels*bpp;
}
inline constexpr std::optional<std::uint64_t> NextEpoch(std::uint64_t epoch){
    if(!epoch||epoch==(std::numeric_limits<std::uint64_t>::max)())return {};
    return epoch+1;
}
}
