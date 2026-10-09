#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// Bounded, in-memory fixture codec. Not a DLL ABI, file loader, diagnostic hot path or live-rights adapter.
#include "ContractManifest.h"
#include <charconv>
#include <cmath>
#include <new>
#include <memory>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace Neurotic::Contracts::Codec
{
struct Limits
{
    static constexpr std::size_t MaxBytes = 1024 * 1024;
    static constexpr std::size_t MaxText = 4096;
    static constexpr std::size_t MaxDepth = 32;
    static constexpr std::size_t MaxNodes = 16384;
    static constexpr std::size_t MaxArray = 256;
    static constexpr std::size_t MaxMembers = 128;
    // New candidate engineering budgets, not measured sizes or runtime capability ceilings.
    static constexpr std::size_t MaxArenaBytes = 16 * 1024 * 1024;
    static constexpr std::size_t MaxConstructionNodes = 65536;
    static constexpr std::size_t MaxTraversalDepth = 64;
    static constexpr std::size_t MaxDecodedValues = 131072;
    static constexpr std::size_t MaxMetadataEntries = 256;
    static constexpr std::size_t MaxTypedObjectBytes = 256 * 1024;
    static constexpr std::size_t MaxTypedScratchBytes = 2 * 1024 * 1024;
    static constexpr std::uint32_t EnvelopeMajor = 2;
};

namespace Detail
{
struct Failure { Error error; };
inline void Require(bool condition, Error error)
{
    if (!condition)
        throw Failure {error};
}
inline void Require(Error error) { Require(error == Error::None, error); }

// One off-hot-path operation owns one fixed allocation arena. No fallback allocation is
// available to DOM strings/vectors, including their abandoned growth buffers. The arena also
// bounds construction work before the final writer sees the tree. It is never imported by M0.
class Arena
{
    std::unique_ptr<std::byte[]> bytes_ {std::make_unique<std::byte[]>(Limits::MaxArenaBytes)};
    std::size_t used_ = 0, constructions_ = 0, traversal_ = 0;
  public:
    void* Allocate(std::size_t bytes, std::size_t alignment)
    {
        Require(used_ <= Limits::MaxArenaBytes, Error::LimitExceeded);
        auto available = Limits::MaxArenaBytes - used_;
        void* next = bytes_.get() + used_;
        void* aligned = std::align(alignment, bytes, next, available);
        Require(aligned != nullptr, Error::LimitExceeded);
        used_ = Limits::MaxArenaBytes - available + bytes;
        return aligned;
    }
    void Node() { Require(++constructions_ <= Limits::MaxConstructionNodes, Error::LimitExceeded); }
    void Enter() { Require(++traversal_ <= Limits::MaxTraversalDepth, Error::LimitExceeded); }
    void Leave() noexcept { --traversal_; }
};
inline thread_local Arena* activeArena = nullptr;
class ArenaScope
{
    Arena arena_;
    Arena* previous_ = activeArena;
  public:
    ArenaScope() { activeArena = &arena_; }
    ~ArenaScope() { activeArena = previous_; }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
};
class TraversalScope
{
    Arena* arena_ = activeArena;
  public:
    TraversalScope() { Require(arena_ != nullptr, Error::Malformed); arena_->Enter(); }
    ~TraversalScope() { arena_->Leave(); }
};
template<class T> struct ArenaAllocator
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    Arena* arena = activeArena;
    ArenaAllocator() noexcept = default;
    template<class U> ArenaAllocator(const ArenaAllocator<U>& other) noexcept : arena(other.arena) {}
    T* allocate(std::size_t count)
    {
        Require(arena != nullptr && count <= Limits::MaxArenaBytes / sizeof(T), Error::LimitExceeded);
        return static_cast<T*>(arena->Allocate(count * sizeof(T), alignof(T)));
    }
    void deallocate(T*, std::size_t) noexcept {}
    template<class U> bool operator==(const ArenaAllocator<U>& other) const noexcept { return arena == other.arena; }
};
using Buffer = std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;
template<class T> using ArrayBuffer = std::vector<T, ArenaAllocator<T>>;
inline std::string_view View(const Buffer& value) noexcept { return {value.data(), value.size()}; }
enum class JsonKind { Null, Boolean, Number, String, Array, Object };
struct Json
{
    JsonKind kind = JsonKind::Null;
    Buffer text;
    ArrayBuffer<Json> children;
    ArrayBuffer<Buffer> keys;
    Json() { Require(activeArena != nullptr, Error::Malformed); activeArena->Node(); }
    Json(Json&&) noexcept = default;
    Json& operator=(Json&&) noexcept = default;
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

inline bool Utf8(std::string_view value) noexcept
{
    std::size_t i = 0;
    while (i < value.size())
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first == 0)
            return false;
        if (first < 0x80)
            continue;
        unsigned need = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { need = 1; code = first & 0x1Fu; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { need = 2; code = first & 0x0Fu; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { need = 3; code = first & 0x07u; minimum = 0x10000; }
        else return false;
        if (value.size() - i < need)
            return false;
        for (unsigned n = 0; n < need; ++n)
        {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0u) != 0x80u)
                return false;
            code = (code << 6u) | (next & 0x3Fu);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return false;
    }
    return true;
}

class Parser
{
    std::string_view input_;
    std::size_t cursor_ = 0, nodes_ = 0;
    char Peek() const noexcept { return cursor_ < input_.size() ? input_[cursor_] : '\0'; }
    char Take()
    {
        Require(cursor_ < input_.size(), Error::Malformed);
        return input_[cursor_++];
    }
    void Space() noexcept
    {
        while (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n')
            ++cursor_;
    }
    void Literal(std::string_view literal)
    {
        Require(input_.substr(cursor_, literal.size()) == literal, Error::Malformed);
        cursor_ += literal.size();
    }
    std::uint32_t Hex4()
    {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const char c = Take();
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else throw Failure {Error::Malformed};
            value = (value << 4u) | digit;
        }
        return value;
    }
    static void Codepoint(Buffer& out, std::uint32_t code)
    {
        Require(code != 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF), Error::Malformed);
        const std::size_t bytes = code < 0x80 ? 1u : code < 0x800 ? 2u : code < 0x10000 ? 3u : 4u;
        Require(bytes <= Limits::MaxText - out.size(), Error::LimitExceeded);
        if (code < 0x80) out.push_back(static_cast<char>(code));
        else if (code < 0x800)
        {
            out.push_back(static_cast<char>(0xC0u | (code >> 6u)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else if (code < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0u | (code >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (code >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((code >> 12u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }
    Buffer String()
    {
        Require(Take() == '"', Error::Malformed);
        Buffer out;
        while (Peek() != '"')
        {
            Require(out.size() < Limits::MaxText, Error::LimitExceeded);
            const auto c = static_cast<unsigned char>(Take());
            Require(c >= 0x20, Error::Malformed);
            if (c != '\\') out.push_back(static_cast<char>(c));
            else
            {
                switch (Take())
                {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    auto code = Hex4();
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        Require(Take() == '\\' && Take() == 'u', Error::Malformed);
                        const auto low = Hex4();
                        Require(low >= 0xDC00 && low <= 0xDFFF, Error::Malformed);
                        code = 0x10000u + ((code - 0xD800u) << 10u) + low - 0xDC00u;
                    }
                    Codepoint(out, code);
                    break;
                }
                default: throw Failure {Error::Malformed};
                }
            }
            Require(out.size() <= Limits::MaxText, Error::LimitExceeded);
        }
        Take();
        Require(Utf8(out), Error::Malformed);
        return out;
    }
    Json Number()
    {
        const auto begin = cursor_;
        if (Peek() == '-') Take();
        if (Peek() == '0') Take();
        else
        {
            Require(Peek() >= '1' && Peek() <= '9', Error::Malformed);
            while (Peek() >= '0' && Peek() <= '9') Take();
        }
        if (Peek() == '.')
        {
            Take();
            Require(Peek() >= '0' && Peek() <= '9', Error::Malformed);
            while (Peek() >= '0' && Peek() <= '9') Take();
        }
        if (Peek() == 'e' || Peek() == 'E')
        {
            Take();
            if (Peek() == '+' || Peek() == '-') Take();
            Require(Peek() >= '0' && Peek() <= '9', Error::Malformed);
            while (Peek() >= '0' && Peek() <= '9') Take();
        }
        Require(cursor_ - begin <= 128, Error::LimitExceeded);
        Json result;
        result.kind = JsonKind::Number;
        result.text.assign(input_.data() + begin, cursor_ - begin);
        return result;
    }
    Json Value(std::size_t depth)
    {
        Require(depth <= Limits::MaxDepth && ++nodes_ <= Limits::MaxNodes, Error::LimitExceeded);
        Space();
        Json result;
        if (Peek() == '{' || Peek() == '[')
        {
            const bool object = Take() == '{';
            const char close = object ? '}' : ']';
            result.kind = object ? JsonKind::Object : JsonKind::Array;
            Space();
            if (Peek() == close) { Take(); return result; }
            for (;;)
            {
                Require(result.children.size() < (object ? Limits::MaxMembers : Limits::MaxArray), Error::LimitExceeded);
                Space();
                if (object)
                {
                    Require(Peek() == '"', Error::Malformed);
                    auto key = String();
                    Require(std::find(result.keys.begin(), result.keys.end(), key) == result.keys.end(), Error::DuplicateField);
                    Space();
                    Require(Take() == ':', Error::Malformed);
                    result.keys.push_back(std::move(key));
                }
                result.children.push_back(Value(depth + 1));
                Space();
                const char separator = Take();
                if (separator == close) break;
                Require(separator == ',', Error::Malformed);
                // Value/String parsing rejects a trailing comma, rather than silently accepting it.
            }
        }
        else if (Peek() == '"') { result.kind = JsonKind::String; result.text = String(); }
        else if (Peek() == 't') { Literal("true"); result.kind = JsonKind::Boolean; result.text = "true"; }
        else if (Peek() == 'f') { Literal("false"); result.kind = JsonKind::Boolean; result.text = "false"; }
        else if (Peek() == 'n') { Literal("null"); }
        else result = Number();
        return result;
    }
  public:
    explicit Parser(std::string_view input) : input_(input) {}
    Json Parse()
    {
        Require(input_.size() <= Limits::MaxBytes, Error::LimitExceeded);
        auto result = Value(0);
        Space();
        Require(cursor_ == input_.size(), Error::Malformed);
        return result;
    }
};

inline Json String(std::string_view text)
{
    Require(text.size() <= Limits::MaxText, Error::LimitExceeded);
    Require(Utf8(text), Error::Malformed);
    Json result;
    result.kind = JsonKind::String;
    result.text.assign(text.data(), text.size());
    return result;
}
inline Json Object() { Json result; result.kind = JsonKind::Object; return result; }
inline Json Array() { Json result; result.kind = JsonKind::Array; return result; }
inline void Add(Json& object, std::string_view key, Json value)
{
    Require(object.kind == JsonKind::Object, Error::WrongValueType);
    Require(key.size() <= Limits::MaxText && Utf8(key), Error::LimitExceeded);
    Require(object.children.size() < Limits::MaxMembers, Error::LimitExceeded);
    Require(std::find(object.keys.begin(), object.keys.end(), key) == object.keys.end(), Error::DuplicateField);
    object.keys.emplace_back(key.data(), key.size());
    object.children.push_back(std::move(value));
}
inline const Json& Get(const Json& object, std::string_view key)
{
    Require(object.kind == JsonKind::Object, Error::WrongValueType);
    for (std::size_t i = 0; i < object.keys.size(); ++i)
        if (object.keys[i] == key)
            return object.children[i];
    throw Failure {Error::MissingField};
}
inline void Exact(const Json& object, std::initializer_list<std::string_view> keys)
{
    Require(object.kind == JsonKind::Object, Error::WrongValueType);
    for (const auto& key : object.keys)
        Require(std::find(keys.begin(), keys.end(), key) != keys.end(), Error::UnknownField);
    Require(object.keys.size() == keys.size(), Error::MissingField);
}

class Writer
{
    Buffer out_;
    std::size_t nodes_ = 0;
    void Append(std::string_view value)
    {
        Require(value.size() <= Limits::MaxBytes - out_.size(), Error::LimitExceeded);
        out_.append(value);
    }
    void Quoted(std::string_view text)
    {
        Require(Utf8(text), Error::Malformed);
        Append("\"");
        for (const unsigned char c : text)
        {
            switch (c)
            {
            case '"': Append("\\\""); break;
            case '\\': Append("\\\\"); break;
            case '\b': Append("\\b"); break;
            case '\f': Append("\\f"); break;
            case '\n': Append("\\n"); break;
            case '\r': Append("\\r"); break;
            case '\t': Append("\\t"); break;
            default:
                if (c < 0x20)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    const char escaped[] {'\\', 'u', '0', '0', hex[c >> 4u], hex[c & 15u]};
                    Append(std::string_view(escaped, 6));
                }
                else
                {
                    const char byte = static_cast<char>(c);
                    Append(std::string_view(&byte, 1));
                }
            }
        }
        Append("\"");
    }
    void Value(const Json& value, std::size_t depth)
    {
        Require(depth <= Limits::MaxDepth && ++nodes_ <= Limits::MaxNodes, Error::LimitExceeded);
        switch (value.kind)
        {
        case JsonKind::Null: Append("null"); break;
        case JsonKind::Boolean: case JsonKind::Number: Append(value.text); break;
        case JsonKind::String: Quoted(value.text); break;
        case JsonKind::Array: case JsonKind::Object:
        {
            const bool object = value.kind == JsonKind::Object;
            Require(value.children.size() <= (object ? Limits::MaxMembers : Limits::MaxArray), Error::LimitExceeded);
            if (object) Require(value.keys.size() == value.children.size(), Error::Malformed);
            Append(object ? "{" : "[");
            for (std::size_t i = 0; i < value.children.size(); ++i)
            {
                if (i != 0) Append(",");
                if (object) { Quoted(value.keys[i]); Append(":"); }
                Value(value.children[i], depth + 1);
            }
            Append(object ? "}" : "]");
            break;
        }
        }
    }
  public:
    std::string Write(const Json& value) { Value(value, 0); return std::string(out_.data(), out_.size()); }
};

template<class T> struct TextInfo { static constexpr bool value = false; };
template<std::size_t N> struct TextInfo<Text<N>> { static constexpr bool value = true; static constexpr auto capacity = N; };
template<class T> struct ListInfo { static constexpr bool value = false; };
template<class T, std::size_t N> struct ListInfo<BoundedList<T, N>>
{ static constexpr bool value = true; using element = T; static constexpr auto capacity = N; };
template<class T> struct OptionalInfo { static constexpr bool value = false; };
template<class T> struct OptionalInfo<std::optional<T>> { static constexpr bool value = true; using element = T; };
template<class T> struct FactInfo { static constexpr bool value = false; };
template<class T> struct FactInfo<OptionalFact<T>> { static constexpr bool value = true; using element = T; };
template<class T> struct VariantInfo { static constexpr bool value = false; };
template<class... T> struct VariantInfo<std::variant<T...>> { static constexpr bool value = true; };
template<class T> struct IdentityInfo { static constexpr bool value = false; };
template<IdentityKind K> struct IdentityInfo<ScopedIdentity<K>> { static constexpr bool value = true; static constexpr auto kind = K; };
template<class T> struct RefInfo { static constexpr bool value = false; };
template<ContractId C> struct RefInfo<ContractRef<C>> { static constexpr bool value = true; static constexpr auto contract = C; };
template<class T> struct MaskInfo { static constexpr bool value = false; };
template<MaskPurpose P> struct MaskInfo<TypedMask<P>> { static constexpr bool value = true; static constexpr auto purpose = P; };
template<class T> struct MetadataInfo { static constexpr bool value = false; };
template<class T> struct MetadataInfo<MetadataRef<T>> { static constexpr bool value = true; using element = T; };
template<class T> struct MetadataListInfo { static constexpr bool value = false; };
template<class T, std::size_t N> struct MetadataListInfo<MetadataList<T, N>>
{ static constexpr bool value = true; using storage = BoundedList<T, N>; static constexpr auto capacity = N; };
template<class> inline constexpr bool AlwaysFalse = false;

template<class T> std::string TypeName()
{
    if constexpr (std::is_same_v<T, bool>) return "Bool";
    else if constexpr (std::is_same_v<T, std::uint64_t>) return "UInt64";
    else if constexpr (std::is_same_v<T, std::uint32_t>) return "UInt32";
    else if constexpr (std::is_same_v<T, double>) return "Float64";
    else if constexpr (std::is_same_v<T, Symbol>) return "Symbol";
    else if constexpr (std::is_same_v<T, ContentRevision>) return "ContentRevision";
    else if constexpr (std::is_same_v<T, ScopeRef>) return "ScopeRef";
    else if constexpr (std::is_same_v<T, GenerationVector>) return "GenerationVector";
    else if constexpr (TextInfo<T>::value) return "Text." + std::to_string(TextInfo<T>::capacity);
    else if constexpr (ListInfo<T>::value) return "List." + TypeName<typename ListInfo<T>::element>() + "." + std::to_string(ListInfo<T>::capacity);
    else if constexpr (MetadataInfo<T>::value) return "MetadataRef." + TypeName<typename MetadataInfo<T>::element>();
    else if constexpr (MetadataListInfo<T>::value) return "MetadataList." + TypeName<typename MetadataListInfo<T>::storage>();
    else if constexpr (OptionalInfo<T>::value) return "NoneOr." + TypeName<typename OptionalInfo<T>::element>();
    else if constexpr (FactInfo<T>::value) return "OptionalFact." + TypeName<typename FactInfo<T>::element>();
    else if constexpr (IdentityInfo<T>::value) return "Identity." + std::string(EnumName(IdentityInfo<T>::kind));
    else if constexpr (RefInfo<T>::value) return "Reference." + std::string(EnumName(RefInfo<T>::contract));
    else if constexpr (MaskInfo<T>::value) return "Mask." + std::string(EnumName(MaskInfo<T>::purpose));
    else if constexpr (std::is_same_v<T, ScalarValue>) return "ScalarValue";
    else if constexpr (std::is_same_v<T, CandidateValue>) return "CandidateValue";
    else if constexpr (requires { T::WireName; }) return std::string(T::WireName);
    else static_assert(AlwaysFalse<T>, "Root/variant alternative needs an explicit symbolic fixture name");
}

template<class T> Json EncodeValue(const T& value);
struct DecodeContext;
template<class T> void DecodeInto(const Json& value, T& out, DecodeContext& context);

template<class T> Json EncodeFields(const T& value)
{
    if constexpr (requires { value.Check(); }) Require(value.Check());
    auto result = Object();
    std::apply([&](const auto&... member) { (Add(result, member.name, EncodeValue(value.*(member.pointer))), ...); }, T::Fields());
    return result;
}
template<class T> Json EncodeValue(const T& value)
{
    TraversalScope traversal;
    if constexpr (std::is_same_v<T, bool>)
    {
        Json result; result.kind = JsonKind::Boolean; result.text = value ? "true" : "false"; return result;
    }
    else if constexpr (std::is_same_v<T, std::uint64_t>) return String(Decimal(value));
    else if constexpr (std::is_same_v<T, ContentRevision>) return EncodeValue(value.value);
    else if constexpr (std::is_same_v<T, std::uint32_t>)
    {
        Json result; result.kind = JsonKind::Number; const auto digits = Decimal(value); result.text.assign(digits.data(), digits.size()); return result;
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        Require(std::isfinite(value), Error::NonFinite);
        std::array<char, 64> buffer {};
        const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                             std::chars_format::general, std::numeric_limits<double>::max_digits10);
        Require(converted.ec == std::errc {}, Error::Malformed);
        Json result; result.kind = JsonKind::Number; result.text.assign(buffer.data(), converted.ptr); return result;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        const auto name = EnumName(value);
        Require(!name.empty(), Error::UnknownEnum);
        return String(name);
    }
    else if constexpr (std::is_same_v<T, Symbol>)
    {
        Require(!value.Empty(), Error::MissingField);
        return String(value.View());
    }
    else if constexpr (TextInfo<T>::value) return String(value.View());
    else if constexpr (OptionalInfo<T>::value) return value ? EncodeValue(*value) : Json {};
    else if constexpr (std::is_same_v<T, ScopeRef>)
    {
        auto out = Object();
        Add(out, "known", EncodeValue(value.key.has_value()));
        if (value.key) Add(out, "identity", EncodeValue(*value.key));
        return out;
    }
    else if constexpr (FactInfo<T>::value)
    {
        auto out = Object();
        Add(out, "known", EncodeValue(value.IsKnown()));
        if (const auto* known = value.KnownPart())
        {
            Add(out, "value", EncodeValue(known->value));
            Add(out, "evidence", EncodeValue(known->evidence));
        }
        else
        {
            const auto* unknown = value.UnknownPart();
            Require(unknown != nullptr, Error::Malformed);
            Add(out, "unknown", EncodeValue(*unknown));
        }
        return out;
    }
    else if constexpr (std::is_same_v<T, GenerationVector>) return EncodeValue(value.Entries());
    else if constexpr (ListInfo<T>::value)
    {
        Require(value.Size() <= Limits::MaxArray, Error::LimitExceeded);
        auto out = Array();
        for (const auto& item : value) out.children.push_back(EncodeValue(item));
        return out;
    }
    else if constexpr (VariantInfo<T>::value)
    {
        auto out = Object();
        std::visit([&](const auto& item) {
            Add(out, "type", String(TypeName<std::remove_cvref_t<decltype(item)>>()));
            Add(out, "value", EncodeValue(item));
        }, value);
        return out;
    }
    else if constexpr (IdentityInfo<T>::value)
    {
        auto out = EncodeFields(value); Add(out, "kind", EncodeValue(IdentityInfo<T>::kind)); return out;
    }
    else if constexpr (MetadataInfo<T>::value)
    {
        auto out = EncodeFields(value);
        Add(out, "metadataType", String(TypeName<typename MetadataInfo<T>::element>()));
        return out;
    }
    else if constexpr (RefInfo<T>::value)
    {
        auto out = EncodeFields(value); Add(out, "contract", EncodeValue(RefInfo<T>::contract)); return out;
    }
    else if constexpr (MaskInfo<T>::value)
    {
        auto out = EncodeFields(value); Add(out, "purpose", EncodeValue(MaskInfo<T>::purpose)); return out;
    }
    else if constexpr (requires { T::Fields(); }) return EncodeFields(value);
    else static_assert(AlwaysFalse<T>, "No fixture serialization for native pointers or unsupported types");
}

// A fixture resolves typed CPU metadata only. RecordKey is reused; this table does not
// allocate operational identity, authenticate publication, retain resources, or issue rights.
struct DecodeContext
{
    struct Entry { const Json* reference = nullptr; const Json* body = nullptr; unsigned state = 0; };
    std::array<Entry, Limits::MaxMetadataEntries> entries {};
    std::size_t count = 0, scratchBytes = 0, depth = 0, decodedValues = 0;
    explicit DecodeContext(const Json* metadata = nullptr)
    {
        if (!metadata) return;
        Require(metadata->kind == JsonKind::Array, Error::WrongValueType);
        Require(metadata->children.size() <= entries.size(), Error::LimitExceeded);
        for (const auto& item : metadata->children)
        {
            Exact(item, {"reference", "value"});
            const auto& reference = Get(item, "reference");
            Require(reference.kind == JsonKind::Object, Error::WrongValueType);
            entries[count++] = {&reference, &Get(item, "value"), 0};
        }
    }
    void Complete() const
    {
        for (std::size_t i = 0; i < count; ++i)
            Require(entries[i].state == 2, Error::Malformed); // No untyped/unreachable hidden payloads.
    }
};
class ScratchScope
{
    DecodeContext& context_;
    std::size_t bytes_;
  public:
    ScratchScope(DecodeContext& context, std::size_t bytes) : context_(context), bytes_(bytes)
    {
        Require(bytes <= Limits::MaxTypedObjectBytes &&
                bytes <= Limits::MaxTypedScratchBytes - context.scratchBytes, Error::LimitExceeded);
        context.scratchBytes += bytes;
    }
    ~ScratchScope() { context_.scratchBytes -= bytes_; }
};
class DecodeDepthScope
{
    DecodeContext& context_;
  public:
    explicit DecodeDepthScope(DecodeContext& context) : context_(context)
    {
        Require(++context_.decodedValues <= Limits::MaxDecodedValues, Error::LimitExceeded);
        Require(++context_.depth <= Limits::MaxTraversalDepth, Error::LimitExceeded);
    }
    ~DecodeDepthScope() { --context_.depth; }
};
template<class T> void DecodeFieldsInto(const Json& value, T& out, DecodeContext& context, std::string_view extra = {})
{
    Require(value.kind == JsonKind::Object, Error::WrongValueType);
    for (const auto& key : value.keys)
    {
        bool known = !extra.empty() && key == extra;
        std::apply([&](const auto&... member) { known = known || ((key == member.name) || ...); }, T::Fields());
        Require(known, Error::UnknownField);
    }
    std::apply([&](const auto&... member) {
        (DecodeInto(Get(value, member.name), out.*(member.pointer), context), ...);
    }, T::Fields());
    if constexpr (requires { out.Check(); }) Require(out.Check());
}
// Decode a reference's fields without resolving it; prevents reference-validation recursion.
template<class T> void DecodeReference(const Json& value, MetadataRef<T>& out, DecodeContext& context)
{
    const auto& type = Get(value, "metadataType");
    Require(type.kind == JsonKind::String && View(type.text) == TypeName<T>(), Error::WrongRecordType);
    DecodeFieldsInto(value, out, context, "metadataType");
}
// Identity comparison does not use serialized ordering, hashes, pointers or coincident values alone.
inline bool SameReferenceIdentity(const Json& a, const Json& b)
{
    const auto& ak = Get(a, "record"); const auto& bk = Get(b, "record");
    for (const auto field : {"owner", "revision", "metadataType"})
    {
        const auto& x = Get(a, field); const auto& y = Get(b, field);
        if (x.kind != y.kind || x.text != y.text) return false;
    }
    for (const auto field : {"nameSpace", "issuer", "value"})
    {
        const auto& x = Get(ak, field); const auto& y = Get(bk, field);
        if (x.kind != y.kind || x.text != y.text) return false;
    }
    // Schema is validated by DecodeReference, but does not permit duplicate owner/key/revision bodies.
    return true;
}
template<class T, class Check> void ResolveMetadata(const Json& reference, const MetadataRef<T>& wanted,
                                                   DecodeContext& context, Check check)
{
    std::size_t found = context.count;
    for (std::size_t i = 0; i < context.count; ++i)
    {
        if (!SameReferenceIdentity(reference, *context.entries[i].reference)) continue;
        Require(found == context.count, Error::DuplicateEntry);
        found = i;
    }
    Require(found != context.count, Error::MissingField);
    auto& entry = context.entries[found];
    Require(entry.state != 1, Error::ContradictoryEntry); // A cycle is not a metadata lifetime policy.
    MetadataRef<T> actual;
    DecodeReference(*entry.reference, actual, context);
    Require(actual == wanted, Error::WrongRecordType);
    // Validate again on repeated uses so count/body constraints of each MetadataList are checked.
    entry.state = 1;
    ScratchScope scratch(context, sizeof(T));
    auto body = std::make_unique<T>();
    DecodeInto(*entry.body, *body, context);
    check(*body);
    entry.state = 2;
}
template<class V, std::size_t I = 0> void DecodeVariantInto(const Json& value, std::string_view type,
                                                         V& out, DecodeContext& context)
{
    if constexpr (I == std::variant_size_v<V>) throw Failure {Error::WrongRecordType};
    else
    {
        using T = std::variant_alternative_t<I, V>;
        if (type == TypeName<T>())
        {
            out.template emplace<I>();
            DecodeInto(value, std::get<I>(out), context);
        }
        else DecodeVariantInto<V, I + 1>(value, type, out, context);
    }
}
template<class T> void DecodeInto(const Json& value, T& out, DecodeContext& context)
{
    DecodeDepthScope depth(context);
    if constexpr (std::is_same_v<T, bool>)
    {
        Require(value.kind == JsonKind::Boolean, Error::WrongValueType); out = value.text == "true";
    }
    else if constexpr (std::is_same_v<T, std::uint64_t>)
    {
        Require(value.kind == JsonKind::String, Error::WrongValueType); Require(ParseDecimal(value.text, out));
    }
    else if constexpr (std::is_same_v<T, ContentRevision>) DecodeInto(value, out.value, context);
    else if constexpr (std::is_same_v<T, std::uint32_t>)
    {
        Require(value.kind == JsonKind::Number, Error::WrongValueType);
        std::uint64_t parsed = 0; Require(ParseDecimal(value.text, parsed));
        Require(parsed <= (std::numeric_limits<std::uint32_t>::max)(), Error::Overflow);
        out = static_cast<std::uint32_t>(parsed);
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        Require(value.kind == JsonKind::Number, Error::WrongValueType);
        const auto result = std::from_chars(value.text.data(), value.text.data() + value.text.size(), out,
                                            std::chars_format::general);
        Require(result.ec != std::errc::result_out_of_range, Error::Overflow);
        Require(result.ec == std::errc {} && result.ptr == value.text.data() + value.text.size(), Error::Malformed);
        Require(std::isfinite(out), Error::NonFinite);
    }
    else if constexpr (std::is_enum_v<T>)
    {
        Require(value.kind == JsonKind::String, Error::WrongValueType);
        Require(ParseEnum(View(value.text), out), Error::UnknownEnum);
    }
    else if constexpr (std::is_same_v<T, Symbol>)
    {
        Require(value.kind == JsonKind::String, Error::WrongValueType);
        Require(value.text.size() <= 96, Error::LimitExceeded); Require(out.Assign(value.text), Error::Malformed);
    }
    else if constexpr (TextInfo<T>::value)
    {
        Require(value.kind == JsonKind::String, Error::WrongValueType);
        Require(value.text.size() <= TextInfo<T>::capacity, Error::LimitExceeded);
        Require(out.Assign(value.text), Error::Malformed);
    }
    else if constexpr (OptionalInfo<T>::value)
    {
        if (value.kind == JsonKind::Null) out.reset();
        else { out.emplace(); DecodeInto(value, *out, context); }
    }
    else if constexpr (std::is_same_v<T, ScopeRef>)
    {
        bool known = false; DecodeInto(Get(value, "known"), known, context);
        if (!known) { Exact(value, {"known"}); out.key.reset(); }
        else { Exact(value, {"known", "identity"}); out.key.emplace(); DecodeInto(Get(value, "identity"), *out.key, context); }
    }
    else if constexpr (FactInfo<T>::value)
    {
        bool known = false; DecodeInto(Get(value, "known"), known, context);
        if (known)
        {
            Exact(value, {"known", "value", "evidence"});
            auto& target = out.EmplaceKnownForConstruction();
            DecodeInto(Get(value, "value"), target.value, context);
            DecodeInto(Get(value, "evidence"), target.evidence, context);
        }
        else
        {
            Exact(value, {"known", "unknown"});
            DecodeInto(Get(value, "unknown"), out.EmplaceUnknownForConstruction(), context);
        }
    }
    else if constexpr (std::is_same_v<T, GenerationVector>)
    {
        Require(value.kind == JsonKind::Array, Error::WrongValueType);
        Require(value.children.size() <= 64, Error::LimitExceeded);
        for (const auto& item : value.children)
        {
            GenerationToken token; DecodeInto(item, token, context); Require(out.Insert(token));
        }
    }
    else if constexpr (ListInfo<T>::value)
    {
        Require(value.kind == JsonKind::Array, Error::WrongValueType);
        Require(value.children.size() <= ListInfo<T>::capacity, Error::LimitExceeded);
        Require(out.Size() == 0, Error::Malformed);
        for (const auto& item : value.children)
        {
            auto* slot = out.AppendDefaultForConstruction(); Require(slot != nullptr, Error::LimitExceeded);
            DecodeInto(item, *slot, context);
        }
    }
    else if constexpr (MetadataInfo<T>::value)
    {
        DecodeReference(value, out, context);
        ResolveMetadata(value, out, context, [](const auto&) {});
    }
    else if constexpr (MetadataListInfo<T>::value)
    {
        Exact(value, {"count", "backing"}); DecodeInto(Get(value, "count"), out.count, context);
        const auto& backing = Get(value, "backing");
        if (backing.kind == JsonKind::Null) out.backing.reset();
        else { out.backing.emplace(); DecodeReference(backing, *out.backing, context); }
        Require(out.Check());
        if (out.backing) ResolveMetadata(backing, *out.backing, context, [&](const auto& body) {
            Require(body.Size() == out.count, Error::Malformed);
        });
    }
    else if constexpr (VariantInfo<T>::value)
    {
        Exact(value, {"type", "value"}); const auto& type = Get(value, "type");
        Require(type.kind == JsonKind::String, Error::WrongValueType);
        DecodeVariantInto(Get(value, "value"), View(type.text), out, context);
    }
    else if constexpr (IdentityInfo<T>::value)
    {
        IdentityKind kind {}; DecodeInto(Get(value, "kind"), kind, context);
        Require(kind == IdentityInfo<T>::kind, Error::WrongIdentityKind);
        DecodeFieldsInto(value, out, context, "kind");
    }
    else if constexpr (RefInfo<T>::value)
    {
        ContractId contract {}; DecodeInto(Get(value, "contract"), contract, context);
        Require(contract == RefInfo<T>::contract, Error::WrongRecordType);
        DecodeFieldsInto(value, out, context, "contract");
    }
    else if constexpr (MaskInfo<T>::value)
    {
        MaskPurpose purpose {}; DecodeInto(Get(value, "purpose"), purpose, context);
        Require(purpose == MaskInfo<T>::purpose, Error::WrongValueType);
        DecodeFieldsInto(value, out, context, "purpose");
    }
    else if constexpr (requires { T::Fields(); }) DecodeFieldsInto(value, out, context);
    else static_assert(AlwaysFalse<T>, "Native pointers and unsupported types cannot be deserialized");
}

inline void CheckExtensions(const Json& extensions)
{
    Require(extensions.kind == JsonKind::Object, Error::WrongValueType);
    // Future MINOR descriptive extensions live only here. Unknown operational fields still fail closed.
    for (std::size_t i = 0; i < extensions.keys.size(); ++i)
    {
        const auto& key = extensions.keys[i];
        Require(key.starts_with("x-"), Error::UnknownField);
        Symbol symbol; Require(symbol.Assign(key), Error::UnsafeExtension);
        std::string lower(key.data(), key.size());
        for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        for (const auto word : {"pointer", "handle", "address", "callback", "dll", "path", "command", "fence", "lease"})
            Require(lower.find(word) == std::string::npos, Error::UnsafeExtension);
        const auto& value = extensions.children[i];
        Require(value.kind == JsonKind::String && value.text.size() <= 256, Error::UnsafeExtension);
        for (const auto c : value.text)
            Require(c != '/' && c != '\\' && static_cast<unsigned char>(c) >= 0x20, Error::UnsafeExtension);
        Require(value.text.find("0x") == std::string::npos && value.text.find("0X") == std::string::npos,
                Error::UnsafeExtension);
    }
}
} // namespace Detail

// Explicitly untrusted fixture storage. All typed objects are private and heap-backed off path;
// publication exposes const values only. No by-value root parameter or live binding exists.
template<class T> class ReplayRecord
{
    std::unique_ptr<T> value_;
    std::string original_;
  public:
    ReplayRecord(std::unique_ptr<T>&& value, std::string&& original)
        : value_(std::move(value)), original_(std::move(original)) {}
    const T& Value() const noexcept { return *value_; }
    std::string_view OriginalText() const noexcept { return original_; }
    ReplayRecord(ReplayRecord&&) noexcept = default;
    ReplayRecord& operator=(ReplayRecord&&) noexcept = default;
    ReplayRecord(const ReplayRecord&) = delete;
    ReplayRecord& operator=(const ReplayRecord&) = delete;
};
struct EncodeResult { std::string text; Error error = Error::None; };
template<class T> struct DecodeResult
{
    std::optional<ReplayRecord<T>> record;
    Error error = Error::None;
};

// Fixture-only catalog, not an operational registry. Original metadata bodies retain owner/ref
// provenance. Capacity and cumulative serialized bytes are checked before storing an entry.
class MetadataCatalog
{
    std::vector<std::string> entries_;
    std::size_t bytes_ = 0;
  public:
    MetadataCatalog() { entries_.reserve(Limits::MaxMetadataEntries); }
    MetadataCatalog(const MetadataCatalog&) = delete;
    MetadataCatalog& operator=(const MetadataCatalog&) = delete;
    std::size_t Size() const noexcept { return entries_.size(); }
    const std::vector<std::string>& Entries() const noexcept { return entries_; }
    Error AddSerialized(std::string_view entry)
    {
        try
        {
            Detail::Require(entries_.size() < Limits::MaxMetadataEntries &&
                            entry.size() <= Limits::MaxBytes - bytes_, Error::LimitExceeded);
            Detail::ArenaScope arena;
            auto parsed = Detail::Parser(entry).Parse();
            Detail::Exact(parsed, {"reference", "value"});
            entries_.emplace_back(entry); bytes_ += entry.size();
            return Error::None;
        }
        catch (const Detail::Failure& failure) { return failure.error; }
        catch (const std::bad_alloc&) { return Error::AllocationFailure; }
        catch (const std::length_error&) { return Error::LimitExceeded; }
    }
    template<class T> Error Add(const MetadataRef<T>& reference, const T& body)
    {
        try
        {
            Detail::Require(entries_.size() < Limits::MaxMetadataEntries, Error::LimitExceeded);
            std::string text;
            {
                Detail::ArenaScope arena;
                auto entry = Detail::Object();
                Detail::Add(entry, "reference", Detail::EncodeValue(reference));
                Detail::Add(entry, "value", Detail::EncodeValue(body));
                text = Detail::Writer {}.Write(entry);
            }
            return AddSerialized(text); // Nested references are validated only with the complete envelope.
        }
        catch (const Detail::Failure& failure) { return failure.error; }
        catch (const std::bad_alloc&) { return Error::AllocationFailure; }
        catch (const std::length_error&) { return Error::LimitExceeded; }
    }
};

template<class T> DecodeResult<T> Decode(std::string_view bytes)
{
    try
    {
        Detail::Require(bytes.size() <= Limits::MaxBytes, Error::LimitExceeded);
        Detail::ArenaScope arena;
        const auto root = Detail::Parser(bytes).Parse();
        Detail::Exact(root, {"schema", "major", "minor", "type", "record", "optionalExtensions", "metadata"});
        Detail::DecodeContext context(&Detail::Get(root, "metadata"));
        const auto& schema = Detail::Get(root, "schema");
        Detail::Require(schema.kind == Detail::JsonKind::String && schema.text == "nr.arch.fixture", Error::UnsupportedVersion);
        std::uint32_t major = 0, minor = 0;
        Detail::DecodeInto(Detail::Get(root, "major"), major, context);
        Detail::DecodeInto(Detail::Get(root, "minor"), minor, context);
        Detail::Require(major == Limits::EnvelopeMajor, Error::UnsupportedVersion);
        const auto& type = Detail::Get(root, "type");
        Detail::Require(type.kind == Detail::JsonKind::String && Detail::View(type.text) == Detail::TypeName<T>(), Error::WrongRecordType);
        Detail::CheckExtensions(Detail::Get(root, "optionalExtensions"));
        Detail::ScratchScope scratch(context, sizeof(T));
        auto value = std::make_unique<T>();
        Detail::DecodeInto(Detail::Get(root, "record"), *value, context);
        context.Complete();
        return {ReplayRecord<T> {std::move(value), std::string(bytes)}, Error::None};
    }
    catch (const Detail::Failure& failure) { return {std::nullopt, failure.error}; }
    catch (const std::bad_alloc&) { return {std::nullopt, Error::AllocationFailure}; }
    catch (const std::length_error&) { return {std::nullopt, Error::LimitExceeded}; }
}

template<class T> EncodeResult EncodeWithMetadata(const T& value, const MetadataCatalog& metadata)
{
    try
    {
        std::string text;
        {
            Detail::ArenaScope arena;
            auto root = Detail::Object();
            Detail::Add(root, "schema", Detail::String("nr.arch.fixture"));
            Detail::Add(root, "major", Detail::EncodeValue(Limits::EnvelopeMajor));
            Detail::Add(root, "minor", Detail::EncodeValue(std::uint32_t {0}));
            Detail::Add(root, "type", Detail::String(Detail::TypeName<T>()));
            Detail::Add(root, "record", Detail::EncodeValue(value));
            Detail::Add(root, "optionalExtensions", Detail::Object());
            auto closure = Detail::Array();
            for (const auto& entry : metadata.Entries()) closure.children.push_back(Detail::Parser(entry).Parse());
            Detail::Add(root, "metadata", std::move(closure));
            text = Detail::Writer {}.Write(root);
        }
        // Validate the complete typed closure after releasing the encoding DOM/arena. This is a
        // bounded fixture-only second pass, never a producer path or an independent test oracle.
        const auto checked = Decode<T>(text);
        if (checked.error != Error::None) return {{}, checked.error};
        return {std::move(text), Error::None};
    }
    catch (const Detail::Failure& failure) { return {{}, failure.error}; }
    catch (const std::bad_alloc&) { return {{}, Error::AllocationFailure}; }
    catch (const std::length_error&) { return {{}, Error::LimitExceeded}; }
}
template<class T> EncodeResult Encode(const T& value)
{
    try { MetadataCatalog empty; return EncodeWithMetadata(value, empty); }
    catch (const std::bad_alloc&) { return {{}, Error::AllocationFailure}; }
    catch (const std::length_error&) { return {{}, Error::LimitExceeded}; }
}
inline Error InspectJson(std::string_view bytes)
{
    try { Detail::ArenaScope arena; (void)Detail::Parser(bytes).Parse(); return Error::None; }
    catch (const Detail::Failure& failure) { return failure.error; }
    catch (const std::bad_alloc&) { return Error::AllocationFailure; }
    catch (const std::length_error&) { return Error::LimitExceeded; }
}
// A fixture consumer can recover the serialized metadata entries for a re-encode. They remain
// untrusted until typed Decode/EncodeWithMetadata validates the full reachable closure.
inline Error ImportMetadata(std::string_view bytes, MetadataCatalog& catalog)
{
    try
    {
        Detail::ArenaScope arena;
        const auto root = Detail::Parser(bytes).Parse();
        const auto& metadata = Detail::Get(root, "metadata");
        Detail::Require(metadata.kind == Detail::JsonKind::Array, Error::WrongValueType);
        for (const auto& entry : metadata.children)
        {
            const auto text = Detail::Writer {}.Write(entry);
            Detail::Require(catalog.AddSerialized(text));
        }
        return Error::None;
    }
    catch (const Detail::Failure& failure) { return failure.error; }
    catch (const std::bad_alloc&) { return Error::AllocationFailure; }
    catch (const std::length_error&) { return Error::LimitExceeded; }
}
} // namespace Neurotic::Contracts::Codec
