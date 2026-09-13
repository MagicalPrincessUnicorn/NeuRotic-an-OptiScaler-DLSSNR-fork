#include "dlssnr/HdrObservation.h"

#include <cstdlib>
#include <iostream>

using namespace DlssNr::HdrObservation;

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    auto& registry = Registry::Instance();
    const auto chain1 = reinterpret_cast<void*>(0x1000);
    const auto chain2 = reinterpret_cast<void*>(0x2000);

    auto first = registry.Register(chain1, DXGI_FORMAT_R10G10B10A2_UNORM);
    Check(first.registered && !first.colorSpaceObserved, "creation starts with an unobserved DXGI SDR default");
    Check(first.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
          "creation does not infer HDR from a 10-bit format");

    const auto failed = registry.RecordColorSpace(chain1, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, E_INVALIDARG);
    Check(!failed.colorSpaceObserved && failed.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
          "failed SetColorSpace1 retains the last successful interpretation");
    Check(failed.colorSpaceResult == E_INVALIDARG, "failed SetColorSpace1 HRESULT is observable");

    const auto pq = registry.RecordColorSpace(chain1, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, S_OK);
    Check(pq.colorSpaceObserved && pq.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
          "successful SetColorSpace1 publishes HDR10 for the matching chain");
    Check(Classify(pq.colorSpace) == ColorClass::Hdr10 && IsHdr(Classify(pq.colorSpace)),
          "HDR10 classification is explicit");

    registry.Register(chain2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    registry.RecordColorSpace(chain2, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, S_OK);
    Check(registry.Read(chain1).colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
          "a second swapchain cannot overwrite the first swapchain");
    Check(Classify(registry.Read(chain2).colorSpace) == ColorClass::ScRgb,
          "scRGB remains distinct from HDR10");

    const auto transition = registry.BeginResize(chain1);
    Check(transition.transitioning, "resize publishes a transition fence");
    const auto rejectedResize = registry.CompleteResize(chain1, DXGI_ERROR_INVALID_CALL, DXGI_FORMAT_UNKNOWN);
    Check(!rejectedResize.transitioning && rejectedResize.resizeGeneration == 0,
          "failed resize ends transition without claiming a successful generation");
    Check(rejectedResize.format == DXGI_FORMAT_R10G10B10A2_UNORM,
          "failed resize retains the prior format");

    registry.BeginResize(chain1);
    const auto resized = registry.CompleteResize(chain1, S_OK, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Check(resized.resizeGeneration == 1 && resized.format == DXGI_FORMAT_R16G16B16A16_FLOAT,
          "successful resize publishes the new format and generation");
    Check(resized.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
          "resize does not invent a color-space transition");

    const unsigned char metadata[] = { 1, 2, 3, 4, 5, 6 };
    const auto meta = registry.RecordMetadata(chain1, DXGI_HDR_METADATA_TYPE_HDR10,
                                               sizeof(metadata), const_cast<unsigned char*>(metadata), S_OK);
    Check(meta.metadataGeneration == 1 && meta.metadataSize == sizeof(metadata) && meta.metadataHash != 0,
          "successful metadata call records bounded evidence");
    const auto rejectedMeta = registry.RecordMetadata(chain1, DXGI_HDR_METADATA_TYPE_NONE,
                                                       0, nullptr, E_INVALIDARG);
    Check(rejectedMeta.metadataGeneration == 1 && rejectedMeta.metadataType == DXGI_HDR_METADATA_TYPE_HDR10 &&
              rejectedMeta.metadataSize == sizeof(metadata) && rejectedMeta.metadataHash == meta.metadataHash,
          "failed metadata call retains the last successful metadata envelope");
    Check(rejectedMeta.requestedMetadataType == DXGI_HDR_METADATA_TYPE_NONE &&
              rejectedMeta.metadataResult == E_INVALIDARG,
          "failed metadata attempt remains observable without becoming active");

    registry.Unregister(chain1);
    Check(!registry.Read(chain1).registered, "destroyed swapchain state is removed");
    const auto reused = registry.Register(chain1, DXGI_FORMAT_R8G8B8A8_UNORM);
    Check(!reused.colorSpaceObserved && reused.metadataGeneration == 0 &&
              reused.format == DXGI_FORMAT_R8G8B8A8_UNORM,
          "pointer reuse cannot inherit stale HDR state");

    registry.Unregister(chain1);
    registry.Unregister(chain2);
    std::cout << "PASS: per-swapchain HDR observation, failed-call retention, resize generations, metadata, and reuse\n";
}
