#define NRW_NATIVE_TEST
#include "../../apps/NeuRoticWindowWorker/nr/NrHost.cpp"
#include <cstring>
#include <dxgi1_6.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <winioctl.h>
namespace
{
void need(bool v, const char *reason)
{
    if (!v)
        throw std::runtime_error(reason);
}
nrw::CapturedFrame Frame(ID3D11Device *d, uint32_t w, uint32_t h, uint64_t seq, bool reset = false)
{
    nrw::CapturedFrame f;
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    f.stamp = {9, seq, uint64_t(q.QuadPart), w, h, reset};
    std::vector<unsigned char> p(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            auto n = (size_t(y) * w + x) * 4;
            p[n] = static_cast<unsigned char>(x * 251 / w);
            p[n + 1] = static_cast<unsigned char>(y * 251 / h);
            p[n + 2] = static_cast<unsigned char>(32 + (x ^ y) % 128);
            p[n + 3] = 255;
        }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{p.data(), w * 4, 0};
    need(SUCCEEDED(d->CreateTexture2D(&td, &data, &f.texture)), "Owned fixture texture create failed");
    return f;
}
std::vector<unsigned char> Read(ID3D11Device *d, ID3D11DeviceContext *c, ID3D11Texture2D *texture)
{
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    auto w = td.Width, h = td.Height;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = td.MiscFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    nrw::ComPtr<ID3D11Texture2D> staging;
    need(SUCCEEDED(d->CreateTexture2D(&td, nullptr, &staging)), "Staging create failed");
    c->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE map{};
    need(SUCCEEDED(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "Actual fixture readback failed");
    std::vector<unsigned char> p(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(p.data() + size_t(y) * w * 4,
                    static_cast<unsigned char *>(map.pData) + size_t(y) * map.RowPitch, w * 4);
    c->Unmap(staging.Get(), 0);
    return p;
}
void Evaluate(nrw::NrHost &host, ID3D11Device *d, ID3D11DeviceContext *c, nrw::CapturedFrame &f,
              const nrw::GuideResult *g, bool expectedEstimated)
{
    const auto before = Read(d, c, f.texture.Get());
    auto r = host.Process(f, g);
    std::cout << "frame=" << f.stamp.sequence << " source=" << f.stamp.width << 'x' << f.stamp.height
              << " work=" << r.workWidth << 'x' << r.workHeight << " recorded=" << r.recorded
              << " submitted=" << r.submitted << " completed=" << r.completed
              << " estimated=" << r.estimatedGuidesUsed << " ms=" << r.milliseconds << " reason=" << r.reason
              << '\n';
    need(r.recorded && r.submitted && r.completed && r.texture, "Actual Feature18 completion unavailable");
    need(r.estimatedGuidesUsed == expectedEstimated, "Actual guide consumption disagrees");
    need(r.texture.Get() != f.texture.Get(), "Output aliases captured original");
    nrw::ComPtr<ID3D11Device> outDevice;
    r.texture->GetDevice(&outDevice);
    need(outDevice.Get() == d, "Output not on exact capture device");
    D3D11_TEXTURE2D_DESC desc{};
    r.texture->GetDesc(&desc);
    need(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && desc.Width == f.stamp.width &&
             desc.Height == f.stamp.height,
         "Completed output shape/format incorrect");
    const auto after = Read(d, c, f.texture.Get());
    need(before == after, "Captured original was modified");
    const auto output = Read(d, c, r.texture.Get());
    need(output.size() == before.size(), "Output cannot be physically read back");
    size_t changed = 0;
    for (size_t n = 0; n < output.size(); n += 4)
        if (std::memcmp(output.data() + n, before.data() + n, 3))
            ++changed;
    std::cout << "  original_immutable=true output_readback=true changed_pixels=" << changed
              << " (difference is not quality evidence)\n";
}
nrw::GuideResult Guides(const nrw::FrameStamp &f)
{
    nrw::GuideResult g;
    g.stamp = f;
    g.width = 518;
    g.height = 294;
    g.estimated = true;
    g.depthCompleted = g.motionCompleted = true;
    g.depthProvider = "controlled-relative-inverse-depth";
    g.motionProvider = "controlled-guide-pixel-current-to-previous";
    g.depth.resize(size_t(g.width) * g.height);
    g.motionXY.assign(size_t(g.width) * g.height * 2, 0);
    for (uint32_t y = 0; y < g.height; ++y)
        for (uint32_t x = 0; x < g.width; ++x)
        {
            auto n = size_t(y) * g.width + x;
            g.depth[n] = float(2 + x) / g.width;
            g.motionXY[n * 2] = .5f;
        }
    return g;
}
} // namespace
namespace nrw
{
void OwnedJunction(const std::filesystem::path &link, const std::filesystem::path &target)
{
    need(CreateDirectoryW(link.c_str(), nullptr) != FALSE, "Owned junction directory create failed");
    const std::wstring substitute = L"\\??\\" + target.wstring(), printable = target.wstring();
    const WORD substituteBytes = WORD(substitute.size() * sizeof(wchar_t)),
               printableBytes = WORD(printable.size() * sizeof(wchar_t));
    const WORD pathBytes = WORD(substituteBytes + sizeof(wchar_t) + printableBytes + sizeof(wchar_t));
    std::vector<unsigned char> buffer(16 + pathBytes);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    const WORD dataBytes = WORD(8 + pathBytes), zero = 0,
               printOffset = WORD(substituteBytes + sizeof(wchar_t));
    std::memcpy(buffer.data(), &tag, 4);
    std::memcpy(buffer.data() + 4, &dataBytes, 2);
    std::memcpy(buffer.data() + 6, &zero, 2);
    std::memcpy(buffer.data() + 8, &zero, 2);
    std::memcpy(buffer.data() + 10, &substituteBytes, 2);
    std::memcpy(buffer.data() + 12, &printOffset, 2);
    std::memcpy(buffer.data() + 14, &printableBytes, 2);
    std::memcpy(buffer.data() + 16, substitute.c_str(), substituteBytes + sizeof(wchar_t));
    std::memcpy(buffer.data() + 16 + printOffset, printable.c_str(), printableBytes + sizeof(wchar_t));
    Handle directory(CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    need(directory.value != INVALID_HANDLE_VALUE, "Owned junction mutation handle failed");
    DWORD returned = 0;
    need(DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()),
                         nullptr, 0, &returned, nullptr) != FALSE,
         "Owned junction reparse creation failed");
}
// Compiled only into the fixture. Exercises the exact production GPU helpers;
// no provider is entered and no NrResult success is returned for this path.
struct NrOwnedGpuFixture
{
    static void DirectoryNamespaceContract()
    {
        const auto fixture =
            std::filesystem::absolute(L"builds/window-worker/nr/namespace-fixture").lexically_normal();
        const auto ancestor = fixture / L"ancestor", selected = ancestor / L"selected",
                   movedLeaf = ancestor / L"selected-moved", movedAncestor = fixture / L"ancestor-moved";
        std::filesystem::create_directories(selected);
        bool leafRenamed = false, ancestorRenamed = false, mutationOpened = false;
        {
            DirectoryLocks lease;
            lease.Open(selected);
            leafRenamed = MoveFileExW(selected.c_str(), movedLeaf.c_str(), 0) != FALSE;
            if (leafRenamed)
                need(MoveFileExW(movedLeaf.c_str(), selected.c_str(), 0) != FALSE,
                     "Owned leaf rename restoration failed");
            ancestorRenamed = MoveFileExW(ancestor.c_str(), movedAncestor.c_str(), 0) != FALSE;
            if (ancestorRenamed)
                need(MoveFileExW(movedAncestor.c_str(), ancestor.c_str(), 0) != FALSE,
                     "Owned ancestor rename restoration failed");
            Handle mutator(CreateFileW(selected.c_str(), GENERIC_WRITE | DELETE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            mutationOpened = mutator.value != INVALID_HANDLE_VALUE;
        }
        bool preexistingMutationRefused = false;
        {
            Handle mutator(CreateFileW(selected.c_str(), GENERIC_WRITE | DELETE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            need(mutator.value != INVALID_HANDLE_VALUE, "Owned preexisting mutation handle fixture failed");
            try
            {
                DirectoryLocks lease;
                lease.Open(selected);
            }
            catch (const std::exception &)
            {
                preexistingMutationRefused = true;
            }
        }
        const auto leafAlias = fixture / L"leaf-alias", ancestorAlias = fixture / L"ancestor-alias";
        OwnedJunction(leafAlias, selected);
        OwnedJunction(ancestorAlias, ancestor);
        bool leafReparseRefused = false, ancestorReparseRefused = false;
        try
        {
            DirectoryLocks lease;
            lease.Open(leafAlias);
        }
        catch (const std::exception &)
        {
            leafReparseRefused = true;
        }
        try
        {
            DirectoryLocks lease;
            lease.Open(ancestorAlias / L"selected");
        }
        catch (const std::exception &)
        {
            ancestorReparseRefused = true;
        }
        // Unlink exact owned junctions without walking their targets. Every
        // remaining removal is nonrecursive and confined to this stable fixture.
        need(RemoveDirectoryW(leafAlias.c_str()) != FALSE && RemoveDirectoryW(ancestorAlias.c_str()) != FALSE,
             "Owned junction unlink failed");
        need(RemoveDirectoryW(selected.c_str()) != FALSE && RemoveDirectoryW(ancestor.c_str()) != FALSE &&
                 RemoveDirectoryW(fixture.c_str()) != FALSE,
             "Owned empty namespace fixture cleanup failed");
        std::cout << "DIRECTORY_NAMESPACE leaf_rename_blocked=" << !leafRenamed
                  << " ancestor_rename_blocked=" << !ancestorRenamed
                  << " reparse_mutation_handle_blocked=" << !mutationOpened
                  << " preexisting_mutation_refused=" << preexistingMutationRefused
                  << " leaf_reparse_refused=" << leafReparseRefused
                  << " ancestor_reparse_refused=" << ancestorReparseRefused << '\n';
        need(!leafRenamed && !ancestorRenamed && !mutationOpened && preexistingMutationRefused &&
                 leafReparseRefused && ancestorReparseRefused,
             "Directory namespace lease admitted rename/reparse race");
    }
    static void TransferFailureContract(ID3D11Device *d, ID3D11DeviceContext *context)
    {
        for (int boundary = 0; boundary < 3; ++boundary)
        {
            NrHost host;
            auto &kit = *host.impl_;
            kit.SetupGpu(d, context);
            kit.Resources(64, 48, 64, 48);
            auto frame = Frame(d, 64, 48, 1);
            bool failed = false;
            kit.fixtureFailIngressSignal = boundary == 0;
            kit.fixtureFailIngressWait = boundary == 1;
            kit.fixtureFailEgressSignal = boundary == 2;
            try
            {
                if (boundary < 2)
                    kit.Ingress(frame.texture.Get());
                else
                {
                    D3D11_TEXTURE2D_DESC td{};
                    frame.texture->GetDesc(&td);
                    NrResult publication; (void)kit.PublishD11(td,publication);
                }
            }
            catch (const std::exception &)
            {
                failed = true;
            }
            need(failed, "Controlled D11 transfer failure was not exercised");
            host.Stop();
            need(host.RequiresRestart(), "Early D11 transfer failure released an unproved owner");
            if (boundary < 2)
                need(kit.pendingInput.Get() == frame.texture.Get() && kit.pendingIngress,
                     "Unproved D11 ingress lost source lifetime");
            else
                need(kit.pendingOutput && kit.pendingEgress, "Unproved D11 egress lost output lifetime");
            std::cout << "D11_TRANSFER_FAILURE boundary=" << boundary
                      << " RequiresRestart=true pending_texture_retained=true\n";
        }
    }
    static void QuarantineContract()
    {
        NrHost host;
        host.impl_->poisoned = true;
        host.Stop();
        need(host.RequiresRestart(), "Quarantined stop lost its retained-work signal");
        std::string reason;
        need(!host.Initialize(nullptr, nullptr, {}, reason) && reason.find("restart") != std::string::npos,
             "Quarantined host reinitialization was admitted");
        host.Stop();
        need(host.RequiresRestart(), "Repeated stop cleared retained-work signal");
        std::cout
            << "API_QUARANTINE RequiresRestart=true reinitialize_refused=true repeated_stop_retained=true\n";
    }
    static void NeutralGuides(ID3D11Device *d, ID3D11DeviceContext *context)
    {
        NrHost host;auto& kit=*host.impl_;kit.SetupGpu(d,context);
        for(auto shape:std::array<std::array<UINT,2>,3>{{{17,9},{65,33},{17,9}}}) {
            kit.depth=kit.Texture(shape[0],shape[1],DXGI_FORMAT_R32_FLOAT);
            kit.motion=kit.Texture(shape[0],shape[1],DXGI_FORMAT_R32G32_FLOAT);
            for(int repeat=0;repeat<2;++repeat) {
                kit.Begin();
                std::vector<float> dirty(SIZE_T(shape[0])*shape[1]*2,23.5f);
                kit.Upload(kit.depth,dirty.data(),1);kit.Upload(kit.motion,dirty.data(),2);kit.Submit();
                kit.Begin();kit.PrepareNeutralGuides();
                const bool noUploads=kit.uploads.empty();
                std::array<ComPtr<ID3D12Resource>,2> buffers;
                std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> footprints{};
                std::array<UINT64,2> sizes{};
                std::array<Image*,2> images{&kit.depth,&kit.motion};
                for(int n=0;n<2;++n) {
                    auto desc=images[n]->resource->GetDesc();
                    kit.d12->GetCopyableFootprints(&desc,0,1,0,&footprints[n],nullptr,nullptr,&sizes[n]);
                    buffers[n]=kit.Buffer(sizes[n],D3D12_HEAP_TYPE_READBACK);
                    kit.Transition(*images[n],D3D12_RESOURCE_STATE_COPY_SOURCE);
                    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=images[n]->resource.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=buffers[n].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[n];
                    kit.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                }
                kit.Submit();
                for(int n=0;n<2;++n) {
                    unsigned char* data=nullptr;D3D12_RANGE read{0,SIZE_T(sizes[n])};
                    require(buffers[n]->Map(0,&read,reinterpret_cast<void**>(&data)),"Neutral guide readback");
                    bool zero=true;
                    for(UINT y=0;y<shape[1];++y)for(UINT x=0;x<shape[0]*UINT(n+1)*sizeof(float);++x)
                        zero=zero && data[footprints[n].Offset+SIZE_T(y)*footprints[n].Footprint.RowPitch+x]==0;
                    D3D12_RANGE noWrite{};buffers[n]->Unmap(0,&noWrite);
                    need(zero,"Neutral guide must overwrite every channel with exact zero");
                }
                need(noUploads,"Neutral preparation must not allocate CPU upload buffers");
            }
        }
        std::cout<<"NEUTRAL_GUIDES exact_zero=true repeated_dirty_clear=true odd_resize=true cpu_uploads=0\n";
    }
    static void Run(ID3D11Device *d, ID3D11DeviceContext *context)
    {
        NrHost host;
        auto &kit = *host.impl_;
        kit.SetupGpu(d, context);
        const std::array<std::array<UINT, 4>, 4> shapes{
            {{1280, 720, 1280, 720}, {1280, 720, 856, 480}, {1280, 720, 640, 360}, {960, 540, 480, 270}}};
        uint64_t seq = 0;
        std::vector<std::pair<NrResult,std::vector<unsigned char>>> retainedOutputs;
        for (auto s : shapes)
        {
            need(kit.Drain(), "Owned GPU fixture previous drain unavailable");
            kit.Resources(s[0], s[1], s[2], s[3]);
            D3D12_HEAP_PROPERTIES outputHeap{};D3D12_HEAP_FLAGS outputFlags{};
            require(kit.output.resource->GetHeapProperties(&outputHeap,&outputFlags),"Inspect output GPU sharing");
            need((outputFlags&D3D12_HEAP_FLAG_SHARED)!=0,"Output must be GPU-shared instead of CPU-published");
            auto frame = Frame(d, s[0], s[1], ++seq);
            const auto untouched = Read(d, context, frame.texture.Get());
            kit.Ingress(frame.texture.Get());
            kit.Begin();
            kit.gpuTimings.Begin();kit.gpuTimings.Mark(kit.list.Get(),0);
            Constants c;
            c.width = s[0];
            c.height = s[1];
            kit.Dispatch(0, c, {&kit.source, nullptr, nullptr, nullptr, nullptr},
                         {&kit.proxy, &kit.original});
            kit.gpuTimings.Mark(kit.list.Get(),1);
            Image *input = &kit.proxy;
            if (s[2] != s[0] || s[3] != s[1])
            {
                c.mode = 2;
                c.width = s[2];
                c.height = s[3];
                kit.Dispatch(1, c, {&kit.proxy, nullptr, nullptr, nullptr, nullptr},
                             {&kit.reducedProxy, nullptr});
                input = &kit.reducedProxy;
            }
            c.mode = 1;
            c.width = s[0];
            c.height = s[1];
            kit.gpuTimings.Mark(kit.list.Get(),2);
            c.apply = 0;
            kit.Dispatch(2, c, {input, input, &kit.original, nullptr, nullptr}, {&kit.output, nullptr});
            kit.gpuTimings.Mark(kit.list.Get(),3);
            kit.Transition(kit.output, D3D12_RESOURCE_STATE_COPY_SOURCE);
            kit.Transition(kit.source, D3D12_RESOURCE_STATE_COMMON);
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = kit.output.resource.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = kit.readback.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            to.PlacedFootprint = kit.footprint;
            kit.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            kit.gpuTimings.Mark(kit.list.Get(),4);kit.gpuTimings.Resolve(kit.list.Get());
            NrMeasurements unavailable;
            kit.gpuTimings.Collect(kit.fence.Get(),kit.submitted+1,unavailable);
            need(!unavailable.gpu[0],"Incomplete GPU query readback must remain unavailable");
            NrResult receipt;kit.Submit(&receipt);
            need(receipt.measurements.gpuCompleted && receipt.measurements.timestampFrequency>0,
                 "Completion receipt must use the actual direct queue clock/fence");
            for(auto span:receipt.measurements.gpu)need(span.has_value()&&*span>=0,"Owned GPU timestamp span unavailable after completion");
            std::cout<<"GPU_TIMING fixture_only=true frequency="<<receipt.measurements.timestampFrequency
                     <<" conversion_ms="<<*receipt.measurements.gpu[0]<<" fence_wait_ms="<<*receipt.measurements.queueWaitMs<<"\n";
            unsigned char *mapped = nullptr;
            D3D12_RANGE range{0, SIZE_T(kit.readbackBytes)};
            require(kit.readback->Map(0, &range, reinterpret_cast<void **>(&mapped)),
                    "Fixture exact GPU readback");
            std::vector<unsigned char> bgra(untouched.size());
            for (UINT y = 0; y < s[1]; ++y)
                for (UINT x = 0; x < s[0]; ++x)
                {
                    auto *src =
                        mapped + kit.footprint.Offset + SIZE_T(y) * kit.footprint.Footprint.RowPitch + x * 4;
                    auto *dst = bgra.data() + (SIZE_T(y) * s[0] + x) * 4;
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = src[3];
                }
            D3D12_RANGE noWrite{};
            kit.readback->Unmap(0, &noWrite);
            need(bgra == untouched, "Current GPU encode/downsample/resolve changed bypass fixture bytes");
            need(Read(d, context, frame.texture.Get()) == untouched,
                 "Shared bridge modified original fixture");
            D3D11_TEXTURE2D_DESC td{};
            frame.texture->GetDesc(&td);
            // Readback above is an independent fixture oracle, never the production publication path.
            kit.Begin();kit.Transition(kit.output,D3D12_RESOURCE_STATE_COMMON);kit.Submit();
            if(retainedOutputs.size()==3) retainedOutputs.erase(retainedOutputs.begin());
            NrResult publication;publication.stamp=frame.stamp;
            auto independent = kit.PublishD11(td,publication);publication.texture=independent;
            need(independent.Get() != frame.texture.Get() && Read(d, context, independent.Get()) == untouched,
                 "Independent same-device completed output differs");
            retainedOutputs.emplace_back(publication,untouched);
            kit.ready=true;kit.options.modelPath=L"C:/inert-fixture/model.dll";kit.options.forwarderPath=L"C:/inert-fixture/forwarder.dll";
            kit.previous=frame.stamp;std::string changeReason;
            NrProcessingOptions live{.42f,.73f,50,2};
            kit.pendingIngress=true;need(!host.Reconfigure(live,changeReason),"Live change refuses unknown ingress retirement");kit.pendingIngress=false;
            kit.pendingEgress=true;need(!host.Reconfigure(live,changeReason),"Live change refuses unknown egress retirement");kit.pendingEgress=false;
            kit.poisoned=true;need(!host.Reconfigure(live,changeReason),"Live change refuses quarantined owner");kit.poisoned=false;
            auto invalid=live;invalid.nrScalePercent=24;need(!host.Reconfigure(invalid,changeReason),"Invalid live scale refuses before mutation");
            need(host.Reconfigure(live,changeReason),"Valid live change completes actual GPU drain");
            need(!kit.previous.sequence&&kit.recreateFeature&&kit.options.modelStyle==2&&kit.options.transferStrength==.42f,"Live style requires feature recreation and temporal reset");
            auto extent=detail::WorkingExtent(kit.options,960,540);need(extent.first==480&&extent.second==272,"Live quality resolves again for resized source");
            need(kit.fence->GetCompletedValue()>=kit.submitted,"Live reconfigure has actual queue completion proof");
            std::cout<<"LIVE_RECONFIGURE actual_drain=true pending_transfers_refused=true temporal_reset=true resize_quality=true (no provider execution)\n";
            kit.options.modelStyle=0;kit.ready=false;

            for(auto& previousOutput:retainedOutputs)need(Read(d,context,previousOutput.first.texture.Get())==previousOutput.second,"Published output remains immutable across next-frame and resize writes");
            std::cout << "COMPOSITION_ONLY source=" << s[0] << 'x' << s[1] << " work=" << s[2] << 'x' << s[3]
                      << " bridge_import=true exact_fence_completed=true current_shader_bypass_exact=true "
                         "original_immutable=true independent_BGRA_readback=true (no model evaluation)\n";
        }
        host.Stop();
    }
};
} // namespace nrw
int wmain(int argc, wchar_t **argv)
{
    try
    {
        nrw::NrOwnedGpuFixture::DirectoryNamespaceContract();
        nrw::NrHost host;
        auto inactive = host.Process({});
        need(!inactive.recorded && !inactive.submitted && !inactive.completed && !inactive.texture,
             "Inactive host reported success");
        host.Stop();
        host.Stop();
        nrw::ComPtr<ID3D11Device> d;
        nrw::ComPtr<ID3D11DeviceContext> c;
        need(SUCCEEDED(D3D11CreateDevice(nullptr, (argc==2 && wcscmp(argv[1],L"--warp-gpu-only")==0)?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d,
                                         nullptr, &c)),
             "Hardware D11 capture device unavailable");
        nrw::ComPtr<IDXGIDevice> dg;
        nrw::ComPtr<IDXGIAdapter> adapter;
        need(SUCCEEDED(d.As(&dg)) && SUCCEEDED(dg->GetAdapter(&adapter)), "Adapter identity unavailable");
        DXGI_ADAPTER_DESC ad{};
        adapter->GetDesc(&ad);
        std::wcout << L"Actual adapter: " << ad.Description << L" vendor=" << ad.VendorId << L"\n";
        nrw::NrOwnedGpuFixture::QuarantineContract();
        nrw::NrOwnedGpuFixture::TransferFailureContract(d.Get(), c.Get());
        nrw::NrOwnedGpuFixture::NeutralGuides(d.Get(), c.Get());
        nrw::NrOwnedGpuFixture::Run(d.Get(), c.Get());
        nrw::NrOptions options;
        std::string reason;
        need(!host.Initialize(d.Get(), c.Get(), options, reason) &&
                 reason == "Please provide your DLSS NR file.",
             "Missing model prerequisite incorrect");
        host.Stop();
        if (argc == 2 && (wcscmp(argv[1], L"--gpu-only") == 0 || wcscmp(argv[1],L"--warp-gpu-only")==0))
        {
            std::cout
                << "Owned transport/composition GPU fixtures passed; no NR provider evaluation attempted.\n";
            return 0;
        }
        if (argc != 3)
        {
            std::cout << "API prerequisite checks passed; physical NR not attempted (pass explicit model and "
                         "forwarder paths).\n";
            return 2;
        }
        options.modelPath = std::filesystem::absolute(argv[1]).wstring();
        options.forwarderPath = std::filesystem::absolute(argv[2]).wstring();
        auto initialize = [&] {
            const bool ok = host.Initialize(d.Get(), c.Get(), options, reason);
            need(ok, reason.c_str());
        };
        initialize();
        auto f = Frame(d.Get(), 1280, 720, 1);
        Evaluate(host, d.Get(), c.Get(), f, nullptr, false);
        f = Frame(d.Get(), 1280, 720, 2);
        Evaluate(host, d.Get(), c.Get(), f, nullptr, false);
        f = Frame(d.Get(), 960, 540, 3, true);
        Evaluate(host, d.Get(), c.Get(), f, nullptr, false);
        host.Stop();
        options.guides = nrw::GuideMode::Shadow;
        initialize();
        f = Frame(d.Get(), 1280, 720, 4, true);
        auto g = Guides(f.stamp);
        Evaluate(host, d.Get(), c.Get(), f, &g, false);
        host.Stop();
        options.guides = nrw::GuideMode::Experimental;
        options.workWidth = 640;
        options.workHeight = 360;
        initialize();
        f = Frame(d.Get(), 1280, 720, 5, true);
        g = Guides(f.stamp);
        g.depthCompleted = false;
        Evaluate(host, d.Get(), c.Get(), f, &g, false);
        f = Frame(d.Get(), 1280, 720, 6);
        g = Guides(f.stamp);
        Evaluate(host, d.Get(), c.Get(), f, &g, true);
        f = Frame(d.Get(), 1280, 720, 7);
        g.stamp.sequence = 6;
        Evaluate(host, d.Get(), c.Get(), f, &g, false);
        host.Stop();
        options.guides = nrw::GuideMode::Off;
        need(host.Initialize(d.Get(), c.Get(), options, reason),
             "Successful release/shutdown did not free standalone ownership");
        host.Stop();
        std::cout << "Physical owned NR fixture passed:7 completed "
                     "evaluations,baseline/history/resize/reset/shadow/experimental/stale,immutable "
                     "original,independent BGRA readbacks,reinitialization after drain/release.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "NR_NATIVE_FAIL: " << e.what() << '\n';
        return 1;
    }
}
