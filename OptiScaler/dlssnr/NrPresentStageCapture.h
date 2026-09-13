#pragma once

#include "NrGpuSafety.h"
#include "NrScreenshotPng.h"
#include "NrScreenshotContract.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <iomanip>

namespace DlssNr::StageCapture
{
struct StageInput
{
    const char* name;
    ID3D12Resource* image;
    D3D12_RESOURCE_STATES state;
    float whitePoint = 0.0f;
};

// Diagnostic only: no source texture or model history is changed. All stages of a
// frame are copied on the same recording, and retained until it cannot be replayed.
class PresentStages
{
  public:
    static constexpr unsigned int MaxFrames = 8;
    static constexpr UINT64 MaxBytes = 512ull * 1024 * 1024;
    static constexpr UINT64 MaxScreenshotBytes = 2ull * 1024 * 1024 * 1024;

    bool active() const { return armed_ || ready_; }
    const std::string& status() const { return status_; }
    unsigned int count() const { return captured_; }
    unsigned int wanted() const { return wanted_; }
    bool wantsFrame(ULONGLONG now) const { return armed_ && !ready_ && now >= start_; }
    void setIdleStatus(std::string value) { if (!active()) status_ = std::move(value); }
    void awaitPublication() { publicationPending_ = true; }
    void completePublication(bool succeeded)
    {
        publicationPending_ = false;
        if (!succeeded) cancel();
    }

    void request(ULONGLONG now, unsigned int delayMs = 5000, unsigned int frames = MaxFrames, bool png = false,
                 std::string buildIdentity = "offline-fixture")
    {
        if (active()) return;
        start_ = now + delayMs;
        limit_ = (std::max)(1u, (std::min)(frames, MaxFrames));
        png_ = png;
        buildIdentity_ = std::move(buildIdentity);
        armed_ = true;
        status_ = delayMs ? "Capture starts in 5 seconds. Close the menu." :
                  png_ ? "Waiting for the next complete output frame." : "Waiting for the next complete NR frame.";
    }

    void cancel()
    {
        if (!active()) return;
        discarded_ = true;
        ready_ = true;
        armed_ = false;
        status_ = "Capture interrupted; waiting for the recorded copies to finish.";
    }

    bool record(ID3D12GraphicsCommandList* cmd, ID3D12Device* device,
                const std::vector<StageInput>& inputs, UINT64 frameId, bool reset,
                const std::string& settings, const Screenshots::Identity& identity = {})
    {
        if (!wantsFrame(GetTickCount64()) || cmd == nullptr || device == nullptr) return false;
        if (inputs.empty() || inputs.size() > 6) { cancel(); return false; }
        if (stages_.empty() && !allocate(device, inputs))
        {
            release();
            return false;
        }
        if (inputs.size() != stages_.size()) { cancel(); return false; }
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (!inputs[i].image || stages_[i].name != inputs[i].name ||
                !sameShape(inputs[i].image->GetDesc(), stages_[i].desc) ||
                inputs[i].whitePoint != stages_[i].whitePoint)
            { cancel(); return false; }
        }
        // Do not silently mix configuration or history discontinuities into a motion sample.
        if (captured_ && (reset || settings != settings_)) { cancel(); return false; }
        const auto ticket = GpuSafety::Record(cmd);
        if (!ticket) { cancel(); return false; }
        settings_ = settings;
        for (size_t i = 0; i < inputs.size(); ++i)
            copy(cmd, inputs[i], stages_[i], captured_);
        frames_.push_back({ticket, frameId, reset, identity});
        ++captured_;
        ready_ = captured_ == wanted_;
        status_ = ready_ ? "Capture recorded. Keep playing while the copies finish."
                         : "Capturing the moving image...";
        return true;
    }

    // Called under the NR lock. File writes happen only after every copy has completed
    // and its command-list recording is sealed. No frame-age approximation is used.
    void poll(const std::filesystem::path& root)
    {
        try { pollFiles(root); }
        catch (...)
        {
            // Batch RAII removes owned files. GPU resources remain retained until
            // the normal completion/discard path or shutdown can release them.
            discarded_ = true;
            status_ = "Capture failed while preparing files. No complete batch was published.";
        }
    }

    void pollFiles(const std::filesystem::path& root)
    {
        if (!ready_ || publicationPending_) return;
        for (const auto& frame : frames_)
        {
            const auto state = GpuSafety::InspectSlots(&frame.use, 1);
            if (state.failed || state.registryFailed)
            {
                status_ = "Capture failed: GPU completion unavailable. Resources retained until shutdown.";
                return;
            }
            if (!GpuSafety::Reusable(frame.use)) return;
        }
        for (const auto& frame : frames_)
            discarded_ = discarded_ || !GpuSafety::Readable(frame.use);
        if (discarded_)
        {
            release();
            status_ = "Capture interrupted by a route, settings or resource change. Please retry.";
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        std::filesystem::path directory;
        std::string prefix;
        HANDLE reservation = INVALID_HANDLE_VALUE;
        bool created = false;
        struct BatchCleanup
        {
            HANDLE& reservation;
            std::filesystem::path directory;
            bool ownDirectory = false, success = false;
            std::vector<std::filesystem::path> staged;
            std::vector<std::pair<std::filesystem::path, bool>> published;
            ~BatchCleanup()
            {
                if (reservation != INVALID_HANDLE_VALUE) CloseHandle(reservation);
                std::error_code error;
                if (!success) for (const auto& [path, owned] : published)
                    if (owned) std::filesystem::remove(path, error);
                for (const auto& path : staged) std::filesystem::remove(path, error);
                if (ownDirectory) std::filesystem::remove(directory, error);
            }
        } cleanup {reservation};
        for (unsigned int attempt = 0; !ec && attempt < 32 && !created; ++attempt)
        {
            if (!png_)
            {
                directory = root / ("capture-" + std::to_string(GetTickCount64()) + "-" + std::to_string(attempt));
                created = std::filesystem::create_directory(directory, ec);
                continue;
            }
            directory = root.parent_path();
            SYSTEMTIME now {};
            GetLocalTime(&now);
            char stamp[64] {};
            std::snprintf(stamp, sizeof(stamp), "Neurotic_%04u-%02u-%02u_%02u-%02u-%02u-%03u_",
                unsigned(now.wYear), unsigned(now.wMonth), unsigned(now.wDay), unsigned(now.wHour),
                unsigned(now.wMinute), unsigned(now.wSecond), unsigned(now.wMilliseconds));
            prefix = stamp + std::to_string(GetCurrentProcessId()) + "_" +
                     std::to_string(GetTickCount64()) + "_" + std::to_string(attempt);
            // Hold an exclusive claim until all files are closed. Concurrent captures
            // cannot select this prefix; the temporary claim disappears even on exit.
            const auto claim = directory / (prefix + "_CAPTURE.pending");
            reservation = CreateFileW(claim.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (reservation == INVALID_HANDLE_VALUE)
            {
                const auto error = GetLastError();
                if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
                continue;
            }
            created = true;
            directory = root.parent_path() / ("." + prefix + "_staging");
            cleanup.directory = directory;
            created = std::filesystem::create_directory(directory, ec);
            cleanup.ownDirectory = created;
            const bool ownDirectory = created;
            for (const auto& stage : stages_)
                for (unsigned int i = 0; !ec && i < captured_; ++i)
                    if (std::filesystem::exists(root / screenshotName(prefix, stage.name, i), ec))
                        created = false;
            if (!created || ec)
            {
                if (ownDirectory) { std::error_code cleanup; std::filesystem::remove(directory, cleanup); }
                cleanup.ownDirectory = false;
                CloseHandle(reservation);
                reservation = INVALID_HANDLE_VALUE;
            }
        }
        bool ok = created && !ec;
        for (const auto& stage : stages_)
            for (unsigned int i = 0; ok && i < captured_; ++i)
            {
                const auto path = directory / (png_ ? screenshotName(prefix, stage.name, i) :
                    stage.name + "_" + std::to_string(i) + ".raw");
                if (png_) cleanup.staged.push_back(path);
                ok = dump(path, stage, i, png_);
            }
        // Every image contains its matching provenance; no user-facing sidecar.
        if (ok && png_)
        {
            std::ostringstream manifest;
            manifest << "{\n  \"schema\": 1,\n  \"batch\": " << Screenshots::JsonString(prefix)
                     << ",\n  \"build_identity\": " << Screenshots::JsonString(buildIdentity_)
                     << ",\n  \"settings\": " << Screenshots::JsonString(settings_)
                     << ",\n  \"brightness_adjustment\": false,\n  \"frames\": [";
            for (size_t i = 0; i < frames_.size(); ++i)
            {
                const auto& frame = frames_[i];
                if (i) manifest << ',';
                manifest << "{\"evaluation\":" << frame.id << ",\"reset\":" << (frame.reset ? "true" : "false")
                         << ",\"route\":" << Screenshots::JsonString(Screenshots::RouteName(frame.identity.route))
                         << ",\"provider_frame\":" << frame.identity.providerFrame
                         << ",\"provider_generation\":" << frame.identity.providerGeneration
                         << ",\"resource_generation\":" << frame.identity.resourceGeneration
                         << ",\"backbuffer\":" << frame.identity.backbuffer << '}';
            }
            manifest << "],\n  \"limitations\": " << Screenshots::JsonString(
                "Native Temporal original-image display conversion can produce a darker reference; experimental. Performance request-only pairs use fresh history, not accumulated live history. Present Enhanced runtime acceptance is pending. No brightness adjustment.")
                << ",\n  \"images\": [";
            bool first = true;
            for (const auto& stage : stages_)
                for (unsigned int i = 0; i < captured_; ++i)
                {
                    if (!first) manifest << ',';
                    first = false;
                    manifest << "{\"file\":" << Screenshots::JsonString(screenshotName(prefix, stage.name, i))
                             << ",\"width\":" << stage.desc.Width << ",\"height\":" << stage.desc.Height
                             << ",\"format\":" << int(stage.desc.Format) << '}';
                }
            manifest << "]\n}\n";
            const auto bytes = manifest.str();
            for (const auto& stage : stages_)
                for (unsigned int i = 0; ok && i < captured_; ++i)
                    ok = Screenshots::EmbedPngManifest(directory / screenshotName(prefix, stage.name, i), bytes);
            for (const auto& stage : stages_)
                for (unsigned int i = 0; ok && i < captured_; ++i)
                {
                    const auto name = screenshotName(prefix, stage.name, i);
                    const auto destination = root / name;
                    cleanup.published.emplace_back(destination, false);
                    ok = MoveFileExW((directory / name).c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
                    cleanup.published.back().second = ok;
                }
        }
        if (ok && !png_)
        {
            const auto path = directory / "manifest.txt";
            if (auto* file = _wfopen(path.wstring().c_str(), L"wb"))
            {
                bool manifestOk = std::fprintf(file, "%s\nframes %u\n%s\n",
                    "Present NR matched stages v1",
                    captured_, settings_.c_str()) >= 0;
                for (const auto& stage : stages_)
                    manifestOk = (std::fprintf(file, "%s width %llu height %u format %d rowPitch %u bytes %llu\n",
                        stage.name.c_str(), stage.desc.Width, stage.desc.Height, (int) stage.desc.Format,
                        stage.layout.Footprint.RowPitch, stage.bytes) >= 0) && manifestOk;
                for (unsigned int i = 0; i < captured_; ++i)
                    manifestOk = (std::fprintf(file, "frame %u evaluation %llu reset %u\n", i,
                        frames_[i].id, frames_[i].reset ? 1u : 0u) >= 0) && manifestOk;
                ok = (std::fclose(file) == 0) && manifestOk;
                if (!ok) std::filesystem::remove(path, ec);
            }
            else ok = false;
        }
        cleanup.success = ok;
        release();
        status_ = ok ? "Saved: " + (png_ ? root / prefix : directory).string() :
                      "Capture could not be saved completely. Check disk space and permissions.";
    }

    // Only after no copies were recorded, Reusable(), or the host's successful shutdown drain.
    void release()
    {
        for (auto& stage : stages_)
            for (auto* resource : stage.shots) if (resource) resource->Release();
        stages_.clear();
        frames_.clear();
        captured_ = wanted_ = 0;
        armed_ = ready_ = discarded_ = false;
        publicationPending_ = false;
        settings_.clear();
    }

  private:
    struct Stage
    {
        std::string name;
        float whitePoint = 0.0f;
        D3D12_RESOURCE_DESC desc {};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout {};
        UINT64 bytes = 0;
        std::vector<ID3D12Resource*> shots;
    };
    struct Frame { GpuSafety::Ticket use; UINT64 id; bool reset; Screenshots::Identity identity; };
    std::vector<Stage> stages_;
    std::vector<Frame> frames_;
    unsigned int captured_ = 0, wanted_ = 0;
    unsigned int limit_ = MaxFrames;
    bool png_ = false;
    ULONGLONG start_ = 0;
    bool armed_ = false, ready_ = false, discarded_ = false;
    bool publicationPending_ = false;
    std::string settings_;
    std::string buildIdentity_;
    std::string status_ = "Ready to capture.";

    std::string screenshotName(const std::string& prefix, const std::string& stage, unsigned int frame) const
    {
        const char* tag = stage == "NR-Off" ? "NROFF" : stage == "Present-NR-On" ? "NRONPRESENT" : "NRON";
        return prefix + (captured_ > 1 ? "_F" + std::to_string(frame) : "") + "_" + tag + ".png";
    }

    static bool supported(const D3D12_RESOURCE_DESC& d)
    {
        return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width && d.Height &&
               d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1 &&
               Screenshots::PixelBytes(d.Format) != 0;
    }
    static bool sameShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b)
    {
        return supported(a) && a.Width == b.Width && a.Height == b.Height && a.Format == b.Format;
    }
    bool allocate(ID3D12Device* device, const std::vector<StageInput>& inputs)
    {
        const UINT64 budget = png_ ? MaxScreenshotBytes : MaxBytes;
        UINT64 bytesPerFrame = 0;
        for (const auto& input : inputs)
        {
            if (!input.image)
            {
                status_ = "Capture failed: an image buffer is missing.";
                return false;
            }
            if (!supported(input.image->GetDesc()))
            {
                const auto d = input.image->GetDesc();
                std::ostringstream reason;
                reason << "Capture failed: unsupported image " << input.name << " (format " << int(d.Format)
                       << ", " << d.Width << "x" << d.Height << ", mips " << d.MipLevels
                       << ", samples " << d.SampleDesc.Count << ").";
                status_ = reason.str();
                return false;
            }
            Stage stage;
            stage.name = input.name;
            stage.whitePoint = input.whitePoint;
            stage.desc = input.image->GetDesc();
            device->GetCopyableFootprints(&stage.desc, 0, 1, 0, &stage.layout, nullptr, nullptr, &stage.bytes);
            if (!stage.bytes || stage.bytes == UINT64_MAX)
            {
                status_ = "Capture failed: DirectX could not describe the image's readback layout.";
                return false;
            }
            if (stage.bytes > budget - bytesPerFrame)
            {
                status_ = "Capture needs more than its " + std::to_string(budget / (1024 * 1024)) +
                          " MiB readback limit for one frame.";
                return false;
            }
            bytesPerFrame += stage.bytes;
            stages_.push_back(std::move(stage));
        }
        wanted_ = static_cast<unsigned int>((std::min)(UINT64(limit_), budget / bytesPerFrame));
        frames_.reserve(wanted_);
        D3D12_HEAP_PROPERTIES heap {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        for (auto& stage : stages_)
        {
            stage.shots.resize(wanted_, nullptr);
            D3D12_RESOURCE_DESC buffer {};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = stage.bytes;
            buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            for (auto& shot : stage.shots)
            {
                const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&shot));
                if (FAILED(result))
                {
                    std::ostringstream reason;
                    reason << "Capture failed: DirectX readback allocation for " << stage.name << " ("
                           << stage.bytes / (1024 * 1024) << " MiB, error 0x" << std::hex
                           << std::uppercase << static_cast<unsigned long>(result) << ").";
                    status_ = reason.str();
                    return false;
                }
            }
        }
        return true;
    }
    static void copy(ID3D12GraphicsCommandList* cmd, const StageInput& input, const Stage& stage, unsigned int i)
    {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {input.image, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              input.state, D3D12_RESOURCE_STATE_COPY_SOURCE};
        if (input.state != D3D12_RESOURCE_STATE_COPY_SOURCE) cmd->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from {}, to {};
        from.pResource = input.image;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource = stage.shots[i];
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = stage.layout;
        cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        if (input.state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            cmd->ResourceBarrier(1, &barrier);
        }
    }
    static bool dump(const std::filesystem::path& path, const Stage& stage, unsigned int i, bool png)
    {
        void* mapped = nullptr;
        const D3D12_RANGE range {0, static_cast<SIZE_T>(stage.bytes)};
        if (FAILED(stage.shots[i]->Map(0, &range, &mapped)) || !mapped) return false;
        bool ok = false;
        if (png)
            ok = Screenshots::WritePng(path, static_cast<const unsigned char*>(mapped),
                static_cast<UINT>(stage.desc.Width), stage.desc.Height, stage.layout.Footprint.RowPitch, stage.desc.Format,
                stage.whitePoint);
        else if (auto* file = _wfopen(path.wstring().c_str(), L"wb"))
        {
            const auto written = std::fwrite(mapped, 1, static_cast<size_t>(stage.bytes), file);
            ok = (std::fclose(file) == 0) && written == stage.bytes;
        }
        const D3D12_RANGE empty {0, 0};
        stage.shots[i]->Unmap(0, &empty);
        return ok;
    }
};
} // namespace DlssNr::StageCapture
