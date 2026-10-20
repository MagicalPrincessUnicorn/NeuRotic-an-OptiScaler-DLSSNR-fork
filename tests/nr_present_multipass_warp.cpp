// Real shipped composition shader, private scratch chain and native MFG wait
// ledger on WARP. Model answers are controlled identity inputs; no vendor NR runs.
#include "nr_color_compose_fixture.h"
#include "../OptiScaler/dlssnr/PreFg.h"
#include "../OptiScaler/dlssnr/DlssNr_Multipass.h"

int main()
{
    Warp w;
    constexpr UINT Width = 16, Height = 8;
    unsigned cases = 0;
    for (bool hdr : {false, true})
    for (unsigned count : {1u, 2u, 4u, 10u})
    for (bool apply : {false, true})
    {
        std::vector<HALF> pixels(Width * Height * 4);
        for (UINT i = 0; i < Width * Height; ++i)
        {
            const float values[] = {hdr ? -0.25f : float(i % 8) / 16.0f,
                hdr ? 12.0f : 0.5f, 0.25f, float(i % 4) / 3.0f};
            for (UINT c = 0; c < 4; ++c) pixels[i * 4 + c] = XMConvertFloatToHalf(values[c]);
        }
        auto target = w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT, Width, Height,
                                D3D12_RESOURCE_STATE_COPY_DEST);
        w.upload(target.Get(), pixels.data(), 8);
        w.run([&](auto* list) { Warp::barrier(list, target.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); });

        for (unsigned pass = 0; pass < count; ++pass)
        {
            // Every child reads the preceding composed result, with separate
            // proxy/original resources. Identical model answers must preserve HDR.
            auto proxy = w.texture(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            auto original = w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT, Width, Height,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            DlssNrConstants constants {};
            constants.Width = Width; constants.Height = Height;
            constants.WhitePoint = hdr ? 2.5f : 1.0f;
            constants.SignedPresent = hdr;
            constants.Transfer = 1;
            constants.TransferStrength = constants.ColourStrength = 1.0f;
            constants.MaxRatio = 2.0f;
            // Earlier passes may seed without applying; the final child remains independent.
            constants.ApplyModel = apply && pass + 1 == count;
            Compose encode(w, constants, target.Get(), nullptr, nullptr, proxy.Get(), original.Get());
            w.run([&](auto* list) {
                Warp::barrier(list, target.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                encode.dispatch(list, Width, Height);
                Warp::barrier(list, target.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Warp::barrier(list, proxy.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Warp::barrier(list, original.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            });
            constants.Mode = 1;
            Compose resolve(w, constants, proxy.Get(), proxy.Get(), original.Get(), target.Get(), nullptr);
            w.run([&](auto* list) { resolve.dispatch(list, Width, Height); });
        }
        const auto actual = w.read(target.Get(), 8);
        if (hdr || !apply) assert(memcmp(actual.data(), pixels.data(), actual.size()) == 0);
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            HALF value;
            memcpy(&value, actual.data() + i * 2, 2);
            assert(std::isfinite(XMConvertHalfToFloat(value)));
            if (i % 4 == 3) assert(value == pixels[i]);
        }

        // All 1-5 generated children consume one completed NR-chain fence, never
        // individual NR passes. Ordered group completion and refusal are real ledger behavior.
        for (unsigned generated = 1; generated <= 5; ++generated)
        {
            DlssNr::PreFg::CompletionLedger ledger;
            ComPtr<ID3D12Fence> fence;
            HR(w.d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
            const auto reservation = ledger.Reserve(target.Get(), 1, 2, 3, 4, 5,
                DlssNr::PreFg::CompletionKind::Output, nullptr, true);
            assert(reservation && ledger.Commit(reservation, target.Get(), fence.Get(), 1));
            assert(ledger.Claim(target.Get(), 1, 2, 3, 4, 5, generated, 2).result ==
                   DlssNr::PreFg::CompletionClaimResult::Refused);
            for (unsigned index = 1; index <= generated; ++index)
            {
                auto claim = ledger.Claim(target.Get(), 1, 2, 3, 4, 5, generated, index);
                assert(claim.result == DlssNr::PreFg::CompletionClaimResult::Ready);
                assert(claim.dependency.fence.Get() == fence.Get() && claim.dependency.value == 1);
                HR(w.q->Wait(claim.dependency.fence.Get(), claim.dependency.value));
            }
            assert(ledger.Claim(target.Get(), 1, 2, 3, 4, 5, generated, generated).result ==
                   DlssNr::PreFg::CompletionClaimResult::Refused);
            HR(fence->Signal(1));
            w.run([](auto*) {}); // prove the actual consumer queue can finish all waits
            ledger.Reset();
            assert(ledger.Count() == 0);
        }
        ++cases;
    }
    w.clean();
    std::cout << "PASS: " << cases << " SDR/HDR 1/2/4/10-pass composition cases; alpha, signed HDR, independent Apply Model and 2x-6x MFG fence groups (WARP; no NR provider)\n";
}
