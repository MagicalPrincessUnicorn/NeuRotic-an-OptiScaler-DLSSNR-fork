#pragma once

#include "NrGpuSafety.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>

namespace DlssNr::StageCapture
{
struct StageInput
{
    const char* name;
    ID3D12Resource* image;
    D3D12_RESOURCE_STATES state;
};

// Diagnostic only: no source texture or model history is changed. All stages of a
// frame are copied on the same recording, and retained until it cannot be replayed.
class PresentStages
{
  public:
    static constexpr unsigned int MaxFrames = 8;
    static constexpr UINT64 MaxBytes = 512ull * 1024 * 1024;

    bool active() const { return armed_ || ready_; }
    const std::string& status() const { return status_; }
    unsigned int count() const { return captured_; }
    unsigned int wanted() const { return wanted_; }
    bool wantsFrame(ULONGLONG now) const { return armed_ && !ready_ && now >= start_; }

    void request(ULONGLONG now, unsigned int delayMs = 5000)
    {
        if (active()) return;
        start_ = now + delayMs;
        armed_ = true;
        status_ = "Capture starts in 5 seconds. Close the menu and turn the camera.";
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
                const std::string& settings)
    {
        if (!wantsFrame(GetTickCount64()) || cmd == nullptr || device == nullptr) return false;
        if (inputs.empty() || inputs.size() > 6) { cancel(); return false; }
        if (stages_.empty() && !allocate(device, inputs))
        {
            release();
            status_ = "Capture could not allocate within its 512 MiB limit. Try a lower workload.";
            return false;
        }
        if (inputs.size() != stages_.size()) { cancel(); return false; }
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (!inputs[i].image || stages_[i].name != inputs[i].name ||
                !sameShape(inputs[i].image->GetDesc(), stages_[i].desc))
            { cancel(); return false; }
        }
        // Do not silently mix configuration or history discontinuities into a motion sample.
        if (captured_ && (reset || settings != settings_)) { cancel(); return false; }
        const auto ticket = GpuSafety::Record(cmd);
        if (!ticket) { cancel(); return false; }
        settings_ = settings;
        for (size_t i = 0; i < inputs.size(); ++i)
            copy(cmd, inputs[i], stages_[i], captured_);
        frames_.push_back({ticket, frameId, reset});
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
        if (!ready_) return;
        for (const auto& frame : frames_)
            if (!GpuSafety::Reusable(frame.use)) return;
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
        bool created = false;
        for (unsigned int attempt = 0; !ec && attempt < 32 && !created; ++attempt)
        {
            directory = root / ("capture-" + std::to_string(GetTickCount64()) + "-" + std::to_string(attempt));
            created = std::filesystem::create_directory(directory, ec);
        }
        bool ok = created && !ec;
        for (const auto& stage : stages_)
            for (unsigned int i = 0; ok && i < captured_; ++i)
                ok = dump(directory / (stage.name + "_" + std::to_string(i) + ".raw"), stage, i);
        // The complete manifest is written last; a partial directory never claims success.
        if (ok)
        {
            const auto path = directory / "manifest.txt";
            if (auto* file = _wfopen(path.wstring().c_str(), L"wb"))
            {
                bool manifestOk = std::fprintf(file, "Present NR matched stages v1\nframes %u\n%s\n",
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
        release();
        status_ = ok ? "Saved: " + directory.string() : "Capture could not be saved completely. Check disk space and permissions.";
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
        settings_.clear();
    }

  private:
    struct Stage
    {
        std::string name;
        D3D12_RESOURCE_DESC desc {};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout {};
        UINT64 bytes = 0;
        std::vector<ID3D12Resource*> shots;
    };
    struct Frame { GpuSafety::Ticket use; UINT64 id; bool reset; };
    std::vector<Stage> stages_;
    std::vector<Frame> frames_;
    unsigned int captured_ = 0, wanted_ = 0;
    ULONGLONG start_ = 0;
    bool armed_ = false, ready_ = false, discarded_ = false;
    std::string settings_;
    std::string status_ = "Ready to capture the model inputs, raw answers and blended image.";

    static bool supported(const D3D12_RESOURCE_DESC& d)
    {
        return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width && d.Height &&
               d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1 &&
               (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                d.Format == DXGI_FORMAT_R10G10B10A2_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    static bool sameShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b)
    {
        return supported(a) && a.Width == b.Width && a.Height == b.Height && a.Format == b.Format;
    }
    bool allocate(ID3D12Device* device, const std::vector<StageInput>& inputs)
    {
        UINT64 bytesPerFrame = 0;
        for (const auto& input : inputs)
        {
            if (!input.image || !supported(input.image->GetDesc())) return false;
            Stage stage;
            stage.name = input.name;
            stage.desc = input.image->GetDesc();
            device->GetCopyableFootprints(&stage.desc, 0, 1, 0, &stage.layout, nullptr, nullptr, &stage.bytes);
            if (!stage.bytes || stage.bytes > MaxBytes - bytesPerFrame) return false;
            bytesPerFrame += stage.bytes;
            stages_.push_back(std::move(stage));
        }
        wanted_ = static_cast<unsigned int>((std::min)(UINT64(MaxFrames), MaxBytes / bytesPerFrame));
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
                if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&shot)))) return false;
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
    static bool dump(const std::filesystem::path& path, const Stage& stage, unsigned int i)
    {
        void* mapped = nullptr;
        const D3D12_RANGE range {0, static_cast<SIZE_T>(stage.bytes)};
        if (FAILED(stage.shots[i]->Map(0, &range, &mapped)) || !mapped) return false;
        bool ok = false;
        if (auto* file = _wfopen(path.wstring().c_str(), L"wb"))
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
