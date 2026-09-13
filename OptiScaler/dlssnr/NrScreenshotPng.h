#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <dxgiformat.h>
#include <filesystem>
#include <vector>
#include <limits>
#include <cstring>
#include <bit>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace DlssNr::Screenshots
{
inline uint32_t PngCrc(const unsigned char* data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
inline std::string ReadPngManifest(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    const auto read32 = [&](size_t p) { return uint32_t(bytes[p]) << 24 | uint32_t(bytes[p+1]) << 16 |
        uint32_t(bytes[p+2]) << 8 | bytes[p+3]; };
    const std::string key = "NeuRotic.CaptureManifest";
    for (size_t pos = 8; pos + 12 <= bytes.size();)
    {
        const auto size = read32(pos);
        if (size > bytes.size() - pos - 12) return {};
        if (std::memcmp(bytes.data()+pos+4, "iTXt", 4) == 0 && size >= key.size()+5 &&
            std::memcmp(bytes.data()+pos+8, key.c_str(), key.size()+1) == 0 &&
            PngCrc(bytes.data()+pos+4, size+4) == read32(pos+size+8))
            return std::string(reinterpret_cast<const char*>(bytes.data()+pos+8+key.size()+5), size-key.size()-5);
        pos += size + 12;
    }
    return {};
}
inline bool EmbedPngManifest(const std::filesystem::path& path, const std::string& manifest)
{
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    if (bytes.size() < 20 || manifest.size() > 1024*1024 ||
        std::memcmp(bytes.data()+bytes.size()-8, "IEND", 4) != 0) return false;
    std::string payload = "NeuRotic.CaptureManifest";
    payload.append(5, '\0'); // keyword, uncompressed, compression method, language, translated keyword
    payload += manifest;
    std::vector<unsigned char> chunk;
    const auto append32 = [&](uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) chunk.push_back(static_cast<unsigned char>(value >> shift));
    };
    append32(static_cast<uint32_t>(payload.size()));
    for (const char c : std::string("iTXt")) chunk.push_back(static_cast<unsigned char>(c));
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    append32(PngCrc(chunk.data()+4, chunk.size()-4));
    bytes.insert(bytes.end()-12, chunk.begin(), chunk.end());
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = bytes.size() <= MAXDWORD && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    ok = CloseHandle(file) && ok;
    return ok && ReadPngManifest(path) == manifest;
}
inline float HalfToFloat(unsigned short h)
{
    const unsigned int sign = unsigned(h & 0x8000u) << 16;
    unsigned int exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    if (exponent == 0)
    {
        if (mantissa == 0) return std::bit_cast<float>(sign);
        int shift = 0;
        while ((mantissa & 1024u) == 0) { mantissa <<= 1; ++shift; }
        return std::bit_cast<float>(sign | (unsigned(113 - shift) << 23) | ((mantissa & 1023u) << 13));
    }
    return std::bit_cast<float>(sign | ((exponent == 31 ? 255u : exponent + 112u) << 23) | (mantissa << 13));
}

inline unsigned int PixelBytes(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_TYPELESS: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    default: return 0;
    }
}

inline float UnsignedFloat(unsigned int bits, unsigned int mantissaBits)
{
    const auto exponent = bits >> mantissaBits;
    const auto mantissa = bits & ((1u << mantissaBits) - 1);
    if (exponent == 31) return 0.0f; // Non-finite pixels cannot be displayed.
    return std::ldexp(float(exponent ? (1u << mantissaBits) + mantissa : mantissa),
                      (exponent ? int(exponent) - 15 : -14) - int(mantissaBits));
}

// Use one fixed exposure for the matched pair. Peak-based Neutwo compression
// preserves channel ratios and highlight gradation; it is an SDR scene preview,
// not an attempt to recreate the game's later tone mapping.
inline void SceneToSrgb(float (&rgb)[3], float whitePoint)
{
    double peak = 0;
    for (auto& value : rgb)
    {
        value = std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f;
        peak = (std::max)(peak, double(value));
    }
    const double divisor = std::hypot(peak, double(whitePoint));
    for (auto& value : rgb)
    {
        const double linear = value / divisor;
        value = float(linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055);
    }
}

// whitePoint == 0 means already display encoded. Positive values explicitly
// request the matched linear-scene conversion. Engine alpha is always omitted.
inline bool WritePng(const std::filesystem::path& path, const unsigned char* pixels,
                     UINT width, UINT height, UINT pitch, DXGI_FORMAT format, float whitePoint = 0.0f)
{
    const bool half = format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
    const bool full = format == DXGI_FORMAT_R32G32B32A32_FLOAT || format == DXGI_FORMAT_R32G32B32A32_TYPELESS ||
                      format == DXGI_FORMAT_R32G32B32_FLOAT;
    const bool packedFloat = format == DXGI_FORMAT_R11G11B10_FLOAT;
    const unsigned int pixelBytes = PixelBytes(format);
    if (!pixels || !width || !height || !pixelBytes || UINT64(width) * pixelBytes > pitch ||
        UINT64(width) * height * 3 > (std::numeric_limits<UINT>::max)() ||
        !std::isfinite(whitePoint) || whitePoint < 0 ||
        (whitePoint > 0 && !half && !full && !packedFloat)) return false;
    struct Apartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) return false;
    WICPixelFormatGUID requested = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&requested)) || requested != GUID_WICPixelFormat24bppBGR) return false;
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3);
    for (UINT y = 0; y < height; ++y)
    {
        const auto* source = pixels + static_cast<size_t>(y) * pitch;
        for (UINT x = 0; x < width; ++x)
        {
            const auto* pixel = source + static_cast<size_t>(x) * pixelBytes;
            auto* out = row.data() + static_cast<size_t>(x) * 3;
            if (half || full || packedFloat)
            {
                float rgb[3] {};
                for (unsigned int channel = 0; channel < 3; ++channel)
                {
                    auto& value = rgb[channel];
                    if (half)
                    {
                        unsigned short word = 0;
                        std::memcpy(&word, pixel + channel * 2, sizeof(word));
                        value = HalfToFloat(word);
                    }
                    else if (full) std::memcpy(&value, pixel + channel * 4, sizeof(value));
                    else
                    {
                        UINT packed = 0; std::memcpy(&packed, pixel, sizeof(packed));
                        value = UnsignedFloat((packed >> (channel * 11)) & (channel == 2 ? 1023u : 2047u),
                                              channel == 2 ? 5u : 6u);
                    }
                }
                if (whitePoint > 0) SceneToSrgb(rgb, whitePoint);
                for (unsigned int channel = 0; channel < 3; ++channel)
                {
                    const auto value = rgb[channel];
                    out[2 - channel] = std::isfinite(value) ?
                        static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f) : 0;
                }
            }
            else if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
            {
                UINT packed = 0; std::memcpy(&packed, pixel, sizeof(packed));
                out[2] = static_cast<unsigned char>(((packed & 1023u) * 255u + 511u) / 1023u);
                out[1] = static_cast<unsigned char>((((packed >> 10) & 1023u) * 255u + 511u) / 1023u);
                out[0] = static_cast<unsigned char>((((packed >> 20) & 1023u) * 255u + 511u) / 1023u);
            }
            else
            {
                const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM;
                out[0] = pixel[bgra ? 0 : 2]; out[1] = pixel[1]; out[2] = pixel[bgra ? 2 : 0];
            }
        }
        if (FAILED(frame->WritePixels(1, width * 3, width * 3, row.data()))) return false;
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

constexpr unsigned int Before = 1, Native = 2, Present = 4;
inline unsigned int AvailableSelection(unsigned int selected, bool present, bool enabled = true, bool nativePair = false)
{
    if (!enabled) return selected & Before;
    return selected & (present ? (Before | Present) : nativePair ? (Before | Native) : Native);
}
} // namespace DlssNr::Screenshots
