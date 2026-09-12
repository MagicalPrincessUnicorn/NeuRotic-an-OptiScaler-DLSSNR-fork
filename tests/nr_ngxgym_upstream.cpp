// Adapted from ngxGym fbaca339271f781f34855a608a5403b973ff332a.
// See ngxGym-MIT-LICENSE.txt. The Present call adds bounded delivery readback;
// native evaluation and shutdown follow the pinned host.
// ngxGym -- a scriptable DLSS host, built to be a game as far as ReShade and the
// dlss5-bridge add-on are concerned, so that the transitions those two fail on can
// be reproduced in seconds instead of by launching Baldur's Gate 3 and playing to a
// particular state.
//
// PHASE 2. A real D3D11 DLSS host driven by a scenario file: display modes,
// resolution changes, quality presets, feature recreation, DLSS switched off and on,
// HDR, and the deliberate misbehaviour a real game cannot be asked to perform.
//
// Three rules that make it honest, all easy to lose:
//
//   D3D11 and DXGI are reached through the IMPORT TABLE and never through
//   LoadLibrary. A LoadLibrary of the system copy walks past ReShade's proxy and the
//   run would prove only that this program can clear a buffer.
//
//   The parameter block is Reset and fully repopulated every frame. If jitter were
//   set once and left, an "omit jitter" later in a scenario would still find last
//   frame's value in the block, the bridge's absence handling would never run, and
//   the run would go green having exercised nothing.
//
//   Every failure is loud and stops the run. A host that limps on after a failed
//   feature creation tests the bridge against a state no game ever presents.
//
//   ..\build.cmd

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cmath>

#include "contract.h"
#include "scenario.h"
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

// ---------------------------------------------------------------------------
// The scene. Deliberately not a renderer: one fullscreen triangle, a procedural
// pattern panned by a constant velocity, depth from a plane equation, and motion
// vectors correct BY CONSTRUCTION rather than by differencing matrices. A bridge fed
// zero motion vectors is not being tested the way a game tests it, and a scene
// elaborate enough to produce real ones would be the bulk of this program.
// ---------------------------------------------------------------------------
static const char kShader[] = R"(
cbuffer C : register(b0)
{
    float2 pan;
    float2 jitter;
    float2 inv_render;
    float2 mv_texel;
    float2 hdr;        // x: linear gain on the scene, y: 1 to PQ-encode (HDR10)
    float2 pad;
};

// ST.2084 OETF, nits in, code value out. The HDR10 swapchain is expected to hold
// this, not linear light; a host that wrote linear 0..1 into it was presenting
// a dim SDR picture under an HDR label.
float3 pq(float3 nits)
{
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 y = pow(saturate(nits / 10000.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut vs(uint id : SV_VertexID)
{
    VSOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.uv  = t;
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.pos.xy += jitter;
    return o;
}

float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }

float noise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(i), hash(i + float2(1, 0)), f.x),
                lerp(hash(i + float2(0, 1)), hash(i + float2(1, 1)), f.x), f.y);
}

struct PSOut { float4 color : SV_Target0; float2 mv : SV_Target1; float depth : SV_Depth; };

PSOut ps(VSOut i)
{
    PSOut o;
    float2 p = i.uv / inv_render + pan;
    float  chk = step(0.5, frac(floor(p.x / 24.0) * 0.5 + floor(p.y / 24.0) * 0.5));
    float  n   = noise(p * 0.08) * 0.6 + noise(p * 0.31) * 0.3;
    float3 c = saturate(float3(chk * 0.8 + n, n, 1.0 - chk * 0.6) * 1.4) * hdr.x;
    if (pad.x > 0.5)
    {
        const float3 patches[16] = {
            float3(0.001,0.001,0.001), float3(0.01,0.01,0.01),
            float3(0.1,0.1,0.1), float3(0.203,0.203,0.203),
            float3(0.4,0.4,0.4), float3(1,1,1), float3(0,0,0), float3(0.05,0.05,0.05),
            float3(0.203,0,0), float3(0,0.203,0), float3(0,0,0.203),
            float3(0.203,0.203,0), float3(0,0.203,0.203), float3(0.203,0,0.203),
            float3(0.15,0.08,0.04), float3(0.02,0.12,0.18)
        };
        c = patches[min((uint)(i.uv.x * 8), 7u) + 8 * min((uint)(i.uv.y * 2), 1u)] * hdr.x;
    }
    if (hdr.y > 0.5) c = pq(c);
    o.color = float4(c, 1);
    o.mv    = mv_texel;
    o.depth = saturate(0.2 + 0.6 * i.uv.y);
    return o;
}

// An explicit scene -> display pipeline for placement comparisons. The source
// is linear BT.709, 1 = diffuse white; the UI is composed AFTER tone mapping.
Texture2D<float4> Scene : register(t0);
float4 display_ps(float4 pos : SV_Position) : SV_Target
{
    uint w, h; Scene.GetDimensions(w, h);
    float3 c = max(Scene.Load(int3(uint2(pos.xy), 0)).rgb, 0.0);
    c *= 203.0 / (1.0 + max(c.r, max(c.g, c.b)) * 203.0 / 1000.0);
    if (pos.y >= h * 0.875)
    {
        const float3 ui[4] = { float3(203,203,203), float3(1,1,1),
                              float3(150,80,40), float3(20,120,180) };
        c = ui[min(uint(pos.x * 4 / w), 3u)];
    }
    const float3x3 to2020 = { .6274039,.3292830,.0433131,
                             .0690973,.9195404,.0113623,
                             .0163914,.0880133,.8955953 };
    return float4(pq(mul(to2020, c)), 1);
}
)";

struct CB { float pan[2]; float jitter[2]; float inv_render[2]; float mv_texel[2]; float hdr[2]; float pad[2]; };

struct Tex
{
    ID3D11Texture2D           *tex = nullptr;
    ID3D11RenderTargetView    *rtv = nullptr;
    ID3D11ShaderResourceView  *srv = nullptr;
    ID3D11UnorderedAccessView *uav = nullptr;
    ID3D11DepthStencilView    *dsv = nullptr;
    void Release()
    {
        if (dsv) { dsv->Release(); dsv = nullptr; }
        if (uav) { uav->Release(); uav = nullptr; }
        if (srv) { srv->Release(); srv = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
    }
};

struct Host
{
    HWND                 hwnd = nullptr;
    ID3D11Device        *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    IDXGIFactory2       *fac = nullptr;
    IDXGISwapChain1     *sc  = nullptr;

    UINT out_w = 1920, out_h = 1080, rw = 0, rh = 0;
    int  quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    bool hdr = false;
    bool colour_chart = false;
    bool scene_pipeline = false;
    bool scene_provider = false;
    // What the scene is multiplied by before it is written, and whether it is
    // PQ-encoded. 1 and off for SDR and plain float; scRGB scales linear light
    // (1.0 = 80 nits) and HDR10 encodes nits.
    float scene_gain = 1.0f;
    bool  scene_pq   = false;
    Mode mode = MODE_WINDOWED;
    DXGI_FORMAT display_fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;

    Tex color, mv, depth, output;
    // Not a fifth slot. Most games supply none, and the four are handled
    // together everywhere; this one is its own thing, exactly as it is in the
    // add-on under test.
    Tex  exposure;
    bool exposure_on = false;
    ID3D11VertexShader      *vs  = nullptr;
    ID3D11PixelShader       *ps  = nullptr;
    ID3D11PixelShader       *display_ps = nullptr;
    ID3D11Buffer            *cb  = nullptr;
    ID3D11DepthStencilState *dss = nullptr;

    NVSDK_NGX_Parameter *caps = nullptr;
    NVSDK_NGX_Parameter *p    = nullptr;
    NVSDK_NGX_Handle    *feat = nullptr;

    bool dlss_on   = true;
    // Rows of padding under every texture: the allocation is taller than the
    // contract, as a game padded for dynamic resolution allocates. The contract
    // and the presented region stay the top rw x rh / out_w x out_h.
    UINT pad = 0;
    bool transpose = false;
    bool stale = false;
    UINT sub_w = 0, sub_h = 0, sub_x = 0, sub_y = 0;   // subrect / subrectat
    Omit omit      = OMIT_NONE;

    int frame = 0, delivered = 0, evaluated = 0;
    RECT     windowed_rect = {};
    LONG_PTR windowed_style = 0;
};

    // The motion vector, computed on the CPU and written verbatim. It used to be
    // "-pan * mv_scale * inv_render" under a comment claiming it was correct by
    // construction, and it was wrong twice over: it multiplied by the very scale
    // NGX multiplies by, and it encoded the CUMULATIVE pan rather than the per-frame
    // delta. At frame 100 with a 1280-wide render that wrote 37.0, which NGX read as
    // 47360 pixels of motion. It is right on frame 1 and only there, which is how it
    // survived being read several times.
    //
    // Doing the arithmetic on the CPU is not a shortcut: it puts the value where it
    // can be printed and checked against MV.Scale, which is the only way anyone was
    // ever going to notice.
static const float kVelX = 0.37f;
static const float kVelY = 0.11f;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static bool MakeTex(ID3D11Device *dev, Tex *t, UINT w, UINT h, DXGI_FORMAT fmt,
                    UINT bind, DXGI_FORMAT srv_fmt = DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT dsv_fmt = DXGI_FORMAT_UNKNOWN)
{
    t->Release();
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = fmt; d.SampleDesc = { 1, 0 };
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &t->tex))) return false;
    if ((bind & D3D11_BIND_RENDER_TARGET) &&
        FAILED(dev->CreateRenderTargetView(t->tex, nullptr, &t->rtv))) return false;
    if ((bind & D3D11_BIND_UNORDERED_ACCESS) &&
        FAILED(dev->CreateUnorderedAccessView(t->tex, nullptr, &t->uav))) return false;
    if (bind & D3D11_BIND_DEPTH_STENCIL)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
        dd.Format = dsv_fmt; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateDepthStencilView(t->tex, &dd, &t->dsv))) return false;
    }
    if (bind & D3D11_BIND_SHADER_RESOURCE)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = srv_fmt != DXGI_FORMAT_UNKNOWN ? srv_fmt : fmt;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(t->tex, &sd, &t->srv))) return false;
    }
    return true;
}

static bool PrintOutputHash(Host &h)
{
    D3D11_TEXTURE2D_DESC d = {};
    h.output.tex->GetDesc(&d);
    const UINT bytes = d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    if (FAILED(h.dev->CreateTexture2D(&d, nullptr, &staging)) || staging == nullptr)
        return false;
    h.ctx->CopyResource(staging, h.output.tex);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(h.ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
    { staging->Release(); return false; }
    unsigned long long hash = 14695981039346656037ull;
    for (UINT y = 0; y < h.out_h; ++y)
    {
        const auto *row = static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch;
        for (size_t x = 0; x < size_t(h.out_w) * bytes; ++x)
            hash = (hash ^ row[x]) * 1099511628211ull;
    }
    h.ctx->Unmap(staging, 0);
    staging->Release();
    printf("OUTPUT frame=%d bytes=%zu hash=%016llx point=after-complete-before-game-copy\n",
           h.frame, size_t(h.out_w) * h.out_h * bytes, hash);
    return true;
}

static bool ParameterResourceUnchanged(NVSDK_NGX_Parameter *p, const char *name,
                                       ID3D11Resource *expected)
{
    ID3D11Resource *observed = nullptr;
    return p->Get(name, &observed) == NVSDK_NGX_Result_Success && observed == expected;
}

// One place, so the create block and the evaluate block cannot drift apart -- which
// is a disagreement real games do have and this host should only produce on purpose.
static unsigned int HostCreateFlags(const Host &h)
{
    unsigned int fl = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (h.hdr) fl |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (h.scene_pipeline && h.hdr) fl |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    return fl;
}

static void ReleaseFeat(Host &h)
{
    if (h.feat != nullptr) { NVSDK_NGX_D3D11_ReleaseFeature(h.feat); h.feat = nullptr; }
}

// Ask NGX for the render size this quality wants, then rebuild the four textures
// and the feature. Every path that changes a shape comes through here, which is why
// there is only one place that can get the order wrong.
static bool Rebuild(Host &h, const char *why)
{
    if (h.caps != nullptr)
    {
        unsigned int maxw = 0, maxh = 0, minw = 0, minh = 0;
        float sharp = 0.0f;
        NGX_DLSS_GET_OPTIMAL_SETTINGS(h.caps, h.out_w, h.out_h,
                                      static_cast<NVSDK_NGX_PerfQuality_Value>(h.quality),
                                      &h.rw, &h.rh, &maxw, &maxh, &minw, &minh, &sharp);
    }
    // A preset the snippet does not offer comes back 0x0, and the host used to
    // run at the output size under the asked-for PerfQualityValue in silence.
    if (h.dlss_on && (h.rw == 0 || h.rh == 0))
    { printf("FAIL: no optimal settings for preset %d\n", h.quality); return false; }
    // With its DLSS off a game renders at the output size, and its depth buffer
    // is the back buffer's size -- which is the one precondition the substitute
    // contract has. Rendering at the DLSS size with DLSS off is what no game
    // does, and it hid a bridge defect: the substitute's contract built with
    // the game's create shape looked right only because both were DLAA here.
    if (!h.dlss_on) { h.rw = h.out_w; h.rh = h.out_h; }

    printf("  rebuild (%s): %ux%u -> %ux%u, quality %d, hdr %d\n",
           why, h.rw, h.rh, h.out_w, h.out_h, h.quality, h.hdr ? 1 : 0);

    // With DLSS off this texture is copied straight to the back buffer. D3D11
    // copies cannot convert float to HDR10/SDR UNORM; render in the target format.
    if (!MakeTex(h.dev, &h.color, h.rw, h.rh + h.pad,
                 (h.dlss_on || h.scene_pipeline) ? DXGI_FORMAT_R16G16B16A16_FLOAT : h.display_fmt,
                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
        !MakeTex(h.dev, &h.mv, h.rw, h.rh + h.pad, DXGI_FORMAT_R16G16_FLOAT,
                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) ||
        !MakeTex(h.dev, &h.depth, h.rw, h.rh + h.pad, DXGI_FORMAT_R32_TYPELESS,
                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL,
                 DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT) ||
        !MakeTex(h.dev, &h.output, h.out_w, h.out_h + h.pad,
                 h.scene_pipeline ? DXGI_FORMAT_R16G16B16A16_FLOAT : h.display_fmt,
                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
    { printf("FAIL: textures\n"); return false; }

    // 1x1 R32_FLOAT, which is the shape every exposure texture in evidence has had.
    // Made once and kept: unlike the four above, its shape never depends on the
    // render size. Kept out of the four deliberately, because most games supply none
    // and the add-on under test keeps its own mirror of it out of its slot array for
    // the same reason.
    if (h.exposure.tex == nullptr)
    {
        if (!MakeTex(h.dev, &h.exposure, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE))
        { printf("FAIL: exposure texture\n"); return false; }
        // A value, not zeroes: an exposure of 0 is not something a game hands
        // over, and the bridge reads the value back and logs it.
        const float one = 1.0f;
        h.ctx->UpdateSubresource(h.exposure.tex, 0, nullptr, &one, sizeof(one), sizeof(one));
    }

    ReleaseFeat(h);

    CreateContract cc = {};
    SetU(&cc.width, h.rw); SetU(&cc.height, h.rh);
    SetU(&cc.out_width, h.out_w); SetU(&cc.out_height, h.out_h);
    SetU(&cc.perf_quality, static_cast<unsigned int>(h.quality));
    if (h.omit != OMIT_FLAGS) SetU(&cc.create_flags, HostCreateFlags(h));
    SetU(&cc.output_subrects, 0);
    SetU(&cc.node_mask_creation, 1); SetU(&cc.node_mask_visibility, 1);

    // A game whose DLSS is off creates no feature on a mode change, and one that
    // never had DLSS creates none at all. The bridge counts creates: one here
    // while the substitute contract holds the session reads as "the game has
    // already used DLSS itself" and refuses the re-arm, which is not what a game
    // with DLSS off does.
    if (!h.dlss_on) { printf("  no feature: dlss is off\n"); return true; }

    h.p->Reset();
    ApplyCreate(h.p, cc);
    const NVSDK_NGX_Result r =
        NVSDK_NGX_D3D11_CreateFeature(h.ctx, NVSDK_NGX_Feature_SuperSampling, h.p, &h.feat);
    if (NVSDK_NGX_FAILED(r) || h.feat == nullptr)
    { printf("FAIL: CreateFeature -> 0x%08X\n", r); return false; }
    return true;
}

// Resize the swapchain to the window's current client area, in the current format,
// then rebuild everything sized to it.
static bool ResizeToWindow(Host &h, const char *why)
{
    RECT c = {};
    GetClientRect(h.hwnd, &c);
    const UINT w = static_cast<UINT>(c.right - c.left);
    const UINT hh = static_cast<UINT>(c.bottom - c.top);
    if (w == 0 || hh == 0) return true;   // minimised; nothing to do yet

    h.color.Release(); h.mv.Release(); h.depth.Release(); h.output.Release();
    const HRESULT hr = h.sc->ResizeBuffers(0, w, hh, h.display_fmt,
                                           DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    if (FAILED(hr)) { printf("FAIL: ResizeBuffers 0x%08lX\n", hr); return false; }
    h.out_w = w; h.out_h = hh;
    return Rebuild(h, why);
}

// Defined below with the window helpers; ApplyMode has to know.
static bool BackgroundMode();

static bool ApplyMode(Host &h, Mode m)
{
    if (m == h.mode) return true;

    // Exclusive fullscreen takes the display whatever anybody wants, so it is
    // the one mode a background run cannot honour. Refused rather than
    // silently downgraded: a scenario that asked for it and got borderless
    // would report on a state it never reached.
    if (m == MODE_EXCLUSIVE && BackgroundMode())
    {
        printf("  mode exclusive: refused, this run is in the background\n");
        return true;
    }


    // Leaving exclusive first, always. Changing window style while the swapchain
    // owns the display leaves the desktop mode changed if anything then fails.
    BOOL fs = FALSE;
    h.sc->GetFullscreenState(&fs, nullptr);
    if (fs && m != MODE_EXCLUSIVE) h.sc->SetFullscreenState(FALSE, nullptr);

    if (m == MODE_EXCLUSIVE)
    {
        const HRESULT hr = h.sc->SetFullscreenState(TRUE, nullptr);
        if (FAILED(hr))
        {
            // Not fatal and worth saying rather than asserting: exclusive fullscreen
            // is refused on some configurations, and a run that reports why is more
            // useful than one that dies.
            printf("  mode exclusive refused 0x%08lX, staying in %s\n", hr,
                   h.mode == MODE_BORDERLESS ? "borderless" : "windowed");
            return true;
        }
    }
    else if (m == MODE_BORDERLESS)
    {
        if (h.mode == MODE_WINDOWED)
        { h.windowed_style = GetWindowLongPtrW(h.hwnd, GWL_STYLE); GetWindowRect(h.hwnd, &h.windowed_rect); }
        HMONITOR mon = MonitorFromWindow(h.hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        SetWindowLongPtrW(h.hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h.hwnd, BackgroundMode() ? HWND_BOTTOM : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW |
                     (BackgroundMode() ? SWP_NOACTIVATE : 0u));
    }
    else
    {
        if (h.windowed_style != 0)
        {
            SetWindowLongPtrW(h.hwnd, GWL_STYLE, h.windowed_style);
            SetWindowPos(h.hwnd, BackgroundMode() ? HWND_BOTTOM : HWND_TOP, h.windowed_rect.left, h.windowed_rect.top,
                         h.windowed_rect.right - h.windowed_rect.left,
                         h.windowed_rect.bottom - h.windowed_rect.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW |
                     (BackgroundMode() ? SWP_NOACTIVATE : 0u));
        }
    }

    h.mode = m;
    return ResizeToWindow(h, "mode change");
}

// Three swapchains a game presents: scRGB float (the default here), HDR10,
// and plain 8-bit SDR. Changing the format is a ResizeBuffers, which is also a
// rebuild -- deliberately the same path a resolution change takes, because
// that is what a game does here too. IsHDR follows HDR10 only.
static bool ApplyDisplay(Host &h, DXGI_FORMAT fmt, DXGI_COLOR_SPACE_TYPE space, bool hdr, const char *why)
{
    if (fmt == h.display_fmt) return true;
    h.hdr = hdr;
    h.display_fmt = fmt;
    if (!ResizeToWindow(h, why)) return false;

    IDXGISwapChain3 *sc3 = nullptr;
    if (SUCCEEDED(h.sc->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3 != nullptr)
    {
        sc3->SetColorSpace1(space);
        sc3->Release();
    }
    return true;
}
// Off, for every mode, is the host's default: float, gamma 2.2 colour space,
// scene as is. The three on-modes are the three presentations a game has:
//   hdr    HDR10: R10G10B10A2, PQ colour space, scene in nits (0..1000) PQ-encoded, IsHDR
//   scrgb  scRGB: float, linear colour space, scene x8 (1.0 = 80 nits, so up to 640), IsHDR
//   sdr    8-bit sRGB, scene as is
static bool ApplyOff(Host &h, const char *why)
{
    h.scene_gain = 1.0f; h.scene_pq = false;
    h.display_fmt = DXGI_FORMAT_UNKNOWN;
    return ApplyDisplay(h, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, false, why);
}
static bool ApplyHdr(Host &h, bool on)
{
    if (!on) return ApplyOff(h, "hdr off");
    h.scene_gain = 1000.0f; h.scene_pq = true;
    return ApplyDisplay(h, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, true, "hdr on");
}
static bool ApplySdr(Host &h, bool on)
{
    if (!on) return ApplyOff(h, "sdr off");
    h.scene_gain = 1.0f; h.scene_pq = false;
    return ApplyDisplay(h, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, false, "sdr on");
}
static bool ApplyScrgb(Host &h, bool on, int nits)
{
    if (!on) return ApplyOff(h, "scrgb off");
    // scRGB: 1.0 is 80 nits, so the scene's brightest pixel lands at nits. A
    // negative peak asks for the gamma-2.2 colour space, see the parser.
    const bool g22 = nits < 0;
    if (nits < 0) nits = -nits;
    h.scene_gain = (nits > 0 ? nits : 640) / 80.0f; h.scene_pq = false;
    // Same format as off, so ApplyDisplay's "unchanged" shortcut would skip the
    // colour space and the flag; force the resize by clearing the format first.
    h.display_fmt = DXGI_FORMAT_UNKNOWN;
    return ApplyDisplay(h, DXGI_FORMAT_R16G16B16A16_FLOAT,
                        g22 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
                        true, g22 ? "scrgb on (g22)" : "scrgb on");
}

static bool RenderFrame(Host &h)
{
    const float jx = h.colour_chart ? 0.0f : Halton((h.frame % 32) + 1, 2) - 0.5f;
    const float jy = h.colour_chart ? 0.0f : Halton((h.frame % 32) + 1, 3) - 0.5f;
    const float panx = static_cast<float>(h.frame) * kVelX;
    const float pany = static_cast<float>(h.frame) * kVelY;
    const float mvsx = -static_cast<float>(h.rw), mvsy = -static_cast<float>(h.rh);

    D3D11_MAPPED_SUBRESOURCE ms = {};
    if (SUCCEEDED(h.ctx->Map(h.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        CB c = {};
        c.pad[0] = h.colour_chart ? 1.0f : 0.0f;
        c.hdr[0] = h.scene_pipeline ? 1000.0f / 203.0f : h.scene_gain;
        c.hdr[1] = !h.scene_pipeline && h.scene_pq ? 1.0f : 0.0f;
        c.pan[0] = panx; c.pan[1] = pany;
        c.jitter[0] = 2.0f * jx / static_cast<float>(h.rw);
        c.jitter[1] = -2.0f * jy / static_cast<float>(h.rh);
        c.inv_render[0] = 1.0f / static_cast<float>(h.rw);
        c.inv_render[1] = 1.0f / static_cast<float>(h.rh);
        // The pattern pans by +kVelX per frame, so a feature now was at x + kVelX
        // last frame; DLSS wants previous-minus-current, which is +kVelX pixels.
        // Divided by MV.Scale because NGX multiplies by it.
        c.mv_texel[0] = h.colour_chart ? 0.0f : kVelX / mvsx;
        c.mv_texel[1] = h.colour_chart ? 0.0f : kVelY / mvsy;
        memcpy(ms.pData, &c, sizeof(c));
        h.ctx->Unmap(h.cb, 0);
    }

    ID3D11RenderTargetView *rts[2] = { h.color.rtv, h.mv.rtv };
    const float clr[4] = { 0, 0, 0, 1 };
    h.ctx->ClearRenderTargetView(h.color.rtv, clr);
    h.ctx->ClearRenderTargetView(h.mv.rtv, clr);
    h.ctx->ClearDepthStencilView(h.depth.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    h.ctx->OMSetRenderTargets(2, rts, h.depth.dsv);
    h.ctx->OMSetDepthStencilState(h.dss, 0);
    D3D11_VIEWPORT vp = { 0, 0, static_cast<float>(h.rw), static_cast<float>(h.rh), 0, 1 };
    h.ctx->RSSetViewports(1, &vp);
    h.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    h.ctx->VSSetShader(h.vs, nullptr, 0);
    h.ctx->PSSetShader(h.ps, nullptr, 0);
    h.ctx->VSSetConstantBuffers(0, 1, &h.cb);
    h.ctx->PSSetConstantBuffers(0, 1, &h.cb);
    // Nine identical draws of the one triangle, on purpose. ReShade's generic
    // depth add-on (examples/09-depth/generic_depth_addon.cpp) does not count a
    // frame in which the only depth-stencil received 8 draw calls or fewer -- a
    // special case for emulators that present more often than they render -- and
    // it skips a depth-stencil whose frame drew 3 vertices or fewer as unused.
    // One fullscreen triangle is exactly one draw of three vertices, so no depth
    // buffer was ever selected here, the DEPTH semantic never bound, and the
    // substitute contract could not arm in this gym at all. The extra draws
    // write the same pixels at the same depth; the picture does not change.
    for (int i = 0; i < 9; ++i) h.ctx->Draw(3, 0);
    ID3D11RenderTargetView *none[2] = { nullptr, nullptr };
    h.ctx->OMSetRenderTargets(2, none, nullptr);

    if (h.scene_provider)
    {
        using SceneFn = HRESULT (*)(ID3D11DeviceContext *, ID3D11Texture2D *, ID3D11Texture2D *, float, float, BOOL);
        const auto scene = reinterpret_cast<SceneFn>(GetProcAddress(
            GetModuleHandleW(L"dlss5-bridge.addon64"), "BridgeSceneExperimentD3D11"));
        if (h.dlss_on || !scene)
        { puts("FAIL: sceneprovider needs nodlss and the experimental bridge export"); return false; }
        if (h.frame == 0)
        {
            ID3D11DeviceContext *deferred = nullptr;
            const bool made = SUCCEEDED(h.dev->CreateDeferredContext(0, &deferred));
            const bool rejected = scene(nullptr, h.color.tex, h.depth.tex, 0, 0, FALSE) == E_INVALIDARG &&
                scene(h.ctx, h.depth.tex, h.color.tex, 0, 0, FALSE) == E_INVALIDARG &&
                made && scene(deferred, h.color.tex, h.depth.tex, 0, 0, FALSE) == E_INVALIDARG;
            if (deferred) deferred->Release();
            if (!rejected) { puts("FAIL: scene provider input validation"); return false; }
            puts("scene provider: null, wrong format and deferred context rejected");
        }
        // Exercise a real history cut, including the OFA warm-up frame.
        if (FAILED(scene(h.ctx, h.color.tex, h.depth.tex, jx, jy, h.frame == 600)))
        { puts("FAIL: scene provider rejected the frame"); return false; }
    }

    if (h.dlss_on && h.feat != nullptr)
    {
        EvalContract ec = {};
        if (h.omit != OMIT_JITTER)  { SetF(&ec.jitter_x, jx);     SetF(&ec.jitter_y, jy); }
        if (h.omit != OMIT_MVSCALE) { SetF(&ec.mv_scale_x, mvsx); SetF(&ec.mv_scale_y, mvsy); }
        SetF(&ec.sharpness, 0.0f);
        SetF(&ec.pre_exposure, 1.0f);
        SetU(&ec.reset, h.frame == 0 ? 1u : 0u);
        // A declared region smaller than the feature, based where the scenario
        // says; the textures stay the feature's size, as PSO2's do (#8).
        const bool region = h.sub_w != 0 && h.sub_h != 0;
        SetU(&ec.subrect_w, region ? h.sub_w : h.rw); SetU(&ec.subrect_h, region ? h.sub_h : h.rh);
        if (region)
        {
            SetU(&ec.in_color_x, h.sub_x); SetU(&ec.in_color_y, h.sub_y);
            SetU(&ec.in_depth_x, h.sub_x); SetU(&ec.in_depth_y, h.sub_y);
            SetU(&ec.in_mv_x, h.sub_x);    SetU(&ec.in_mv_y, h.sub_y);
            SetU(&ec.out_x, 0u);           SetU(&ec.out_y, 0u);
        }
        if (h.omit != OMIT_FLAGS) SetU(&ec.create_flags, HostCreateFlags(h));
        if (h.omit != OMIT_QUALITY) SetU(&ec.perf_quality, static_cast<unsigned int>(h.quality));

        h.p->Reset();
        ApplyEval(h.p, ec);

        // The misbehaviour. Baldur's Gate 3 emitted exactly this after a borderless
        // to fullscreen change: the four scalars transposed in the EVALUATE block
        // while the images it handed over stayed honest, and it did not stop.
        if (h.transpose)
        {
            h.p->Set("Width",     h.out_w); h.p->Set("Height",    h.out_h);
            h.p->Set("OutWidth",  h.rw);    h.p->Set("OutHeight", h.rh);
        }
        // The other misbehaviour. Baldur's Gate 3's character creator creates a
        // preview feature through the same parameter block, and from then on
        // every evaluate of the MAIN feature carries the preview's four scalars
        // -- 1920x1080 -> 1280x720 on a 3413x960 -> 5120x1440 session -- while. Here
        // 960x540 -> 640x360, which is deliberately NOT a transposition of this
        // host's own 1280x720 -> 1920x1080: the swap repair alone would fix that.
        // the textures and DLSS.Render.Subrect.Dimensions stay honest (#18).
        if (h.stale)
        {
            h.p->Set("Width",      960u); h.p->Set("Height",     540u);
            h.p->Set("OutWidth",   640u); h.p->Set("OutHeight",  360u);
        }

        h.p->Set("Color",  static_cast<ID3D11Resource *>(h.color.tex));
        h.p->Set("Output", static_cast<ID3D11Resource *>(h.output.tex));
        h.p->Set("Depth",  static_cast<ID3D11Resource *>(h.depth.tex));
        h.p->Set("MotionVectors", static_cast<ID3D11Resource *>(h.mv.tex));
        // The create flags deliberately never carry AutoExposure, so switching this
        // on is the Bannerlord and Red Dead Redemption 2 shape: DLSS is driven from
        // an exposure texture and told nothing about it.
        if (h.exposure_on)
        {
            h.p->Set("ExposureTexture", static_cast<ID3D11Resource *>(h.exposure.tex));
            // Read straight back, once. The add-on reported "no such key here" for
            // this key across a whole run where the host believed it was setting it,
            // and there was no way to tell whether the host's Set never landed or the
            // add-on was reading a different block. One line settles it, on the same
            // object, in the same frame.
            static bool said = false;
            if (!said)
            {
                said = true;
                ID3D11Resource *back = nullptr;
                const NVSDK_NGX_Result g = h.p->Get("ExposureTexture", &back);
                printf("  ExposureTexture set=%p, read back 0x%08X -> %p\n",
                       static_cast<void *>(h.exposure.tex), g, static_cast<void *>(back));
            }
        }

        const NVSDK_NGX_Result r = NVSDK_NGX_D3D11_EvaluateFeature(h.ctx, h.feat, h.p, nullptr);
        ++h.evaluated;
        if (NVSDK_NGX_SUCCEED(r)) ++h.delivered;
        else if (h.delivered == 0 || (h.evaluated % 600) == 0)
            printf("  EvaluateFeature frame %d -> 0x%08X\n", h.frame, r);
        if (!ParameterResourceUnchanged(h.p, "Color", h.color.tex) ||
            !ParameterResourceUnchanged(h.p, "Output", h.output.tex) ||
            !ParameterResourceUnchanged(h.p, "Depth", h.depth.tex) ||
            !ParameterResourceUnchanged(h.p, "MotionVectors", h.mv.tex))
        { puts("FAIL: native evaluation resource parameter changed"); return false; }
        if ((h.frame == 60 || h.frame == 120 || h.frame == 240) && !PrintOutputHash(h))
        { puts("FAIL: native output hash unavailable"); return false; }
    }


    // Read one motion-vector texel back, once, and print what NGX will make of it.
    // The previous version of this scene carried a comment claiming the vectors were
    // correct by construction; they were wrong by a factor of 128000 and nobody could
    // have seen it, because nothing printed a number. A comment asserting correctness
    // with no runnable check behind it is the same failure as a PASS with no
    // assertion. Printed, not asserted -- same cadence as the ExposureTexture
    // read-back below, and for the same reason.
    if (h.frame == 2)
    {
        D3D11_TEXTURE2D_DESC sd = {};
        h.mv.tex->GetDesc(&sd);
        sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
        ID3D11Texture2D *st = nullptr;
        if (SUCCEEDED(h.dev->CreateTexture2D(&sd, nullptr, &st)) && st != nullptr)
        {
            h.ctx->CopyResource(st, h.mv.tex);
            D3D11_MAPPED_SUBRESOURCE ms2 = {};
            if (SUCCEEDED(h.ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms2)))
            {
                // R16G16_FLOAT: two halves. Converted by hand rather than pulling in
                // a library for four lines.
                const unsigned short *px = static_cast<const unsigned short *>(ms2.pData);
                auto half = [](unsigned short v) {
                    const int s2 = (v >> 15) & 1, e = (v >> 10) & 0x1F, m = v & 0x3FF;
                    float f = 0.0f;
                    if (e == 0)      f = m / 1024.0f / 16384.0f;
                    else if (e < 31) f = (1.0f + m / 1024.0f) * powf(2.0f, static_cast<float>(e - 15));
                    return s2 ? -f : f;
                };
                const float tx = half(px[0]), ty = half(px[1]);
                printf("  MV texel (%.8f, %.8f) x MV.Scale (%.1f, %.1f) = (%.3f, %.3f) px,"
                       " scene velocity is (%.2f, %.2f)\n",
                       tx, ty, mvsx, mvsy, tx * mvsx, ty * mvsy, kVelX, kVelY);
                h.ctx->Unmap(st, 0);
            }
            st->Release();
        }
    }
    ID3D11Texture2D *bb = nullptr;
    if (SUCCEEDED(h.sc->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb != nullptr)
    {
        // Both arms of this were the same texture, under a comment describing a
        // choice that was never made -- so the DLSS-off segment presented one frozen
        // upscaled frame for nine hundred frames. The colour target is the render
        // resolution and the back buffer is the output resolution, so it cannot be
        // a CopyResource; CopySubresourceRegion places it in the corner, which is
        // ugly and is not the point. What matters is that presents keep happening
        // and that what is presented CHANGES, because a frozen image is
        // indistinguishable from a bridge that has stopped.
        if (h.scene_pipeline)
        {
            if (!h.hdr || h.pad || h.rw != h.out_w || h.rh != h.out_h)
            { puts("FAIL: scenepipeline requires HDR10, DLAA/full resolution and no padding"); bb->Release(); return false; }
            ID3D11RenderTargetView *target = nullptr;
            if (FAILED(h.dev->CreateRenderTargetView(bb, nullptr, &target)))
            { puts("FAIL: scene display RTV"); bb->Release(); return false; }
            ID3D11ShaderResourceView *src = h.dlss_on ? h.output.srv : h.color.srv;
            h.ctx->OMSetRenderTargets(1, &target, nullptr);
            h.ctx->PSSetShaderResources(0, 1, &src);
            h.ctx->PSSetShader(h.display_ps, nullptr, 0);
            h.ctx->Draw(3, 0);
            src = nullptr;
            h.ctx->PSSetShaderResources(0, 1, &src);
            h.ctx->OMSetRenderTargets(0, nullptr, nullptr);
            target->Release();
        }
        else if (h.dlss_on && h.feat != nullptr)
        {
            // The region, not the resource: with padding the output allocation
            // is taller than the back buffer.
            D3D11_BOX box = { 0, 0, 0, h.out_w, h.out_h, 1 };
            h.ctx->CopySubresourceRegion(bb, 0, 0, 0, 0, h.output.tex, 0, &box);
        }
        else
        {
            D3D11_BOX box = { 0, 0, 0, h.rw, h.rh, 1 };
            h.ctx->CopySubresourceRegion(bb, 0, 0, 0, 0, h.color.tex, 0, &box);
        }
        bb->Release();
    }
    const HRESULT phr = ObservePresent(h.sc, h.ctx, h.frame);
    if (phr == DXGI_ERROR_DEVICE_REMOVED || phr == DXGI_ERROR_DEVICE_RESET)
    { printf("FAIL: Present -> 0x%08lX, device removed (reason 0x%08lX)\n", phr, h.dev->GetDeviceRemovedReason()); return false; }
    ++h.frame;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return true;
}


// Run without taking the screen. NGXGYM_BACKGROUND=1 in the environment creates
// the window without activating it and drops it to the bottom of the z-order, so
// a suite can run while somebody works.
//
// NOT minimised: a minimised window has a zero-size client area, the swapchain
// extent goes to 0x0 and the Vulkan half refuses to build one -- correctly, since
// there is nothing to present to. Occluded is the state that keeps rendering.
//
// WS_EX_TOOLWINDOW keeps it out of alt-tab and the taskbar as well.
static bool BackgroundMode()
{
    static int cached = -1;
    if (cached < 0)
    {
        char v[8] = {};
        cached = (GetEnvironmentVariableA("NGXGYM_BACKGROUND", v, sizeof(v)) != 0 &&
                  v[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

static void ShowHostWindow(HWND w)
{
    if (!BackgroundMode()) { ShowWindow(w, SW_SHOW); return; }
    ShowWindow(w, SW_SHOWNOACTIVATE);
    SetWindowPos(w, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    printf("background: the window is shown without focus and sent to the back. "
           "Exclusive fullscreen is refused in this mode.\n");
}


// What the driver put into this process, and what the swapchain says it
// presented. Printed at the end of every run so a driver-side feature that has
// no API of its own -- Smooth Motion is one -- can be told apart by what it
// changes: the NVIDIA modules loaded, and the present statistics. Compare a run
// with the feature on against one with it off; the line that differs is the
// detector, and there is none until that comparison has been made.
typedef BOOL (WINAPI *PFN_EnumProcessModules)(HANDLE, HMODULE *, DWORD, DWORD *);
typedef DWORD (WINAPI *PFN_GetModuleFileNameExA)(HANDLE, HMODULE, LPSTR, DWORD);
static void PrintNvModules()
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    PFN_EnumProcessModules   enumMods = reinterpret_cast<PFN_EnumProcessModules>(GetProcAddress(k32, "K32EnumProcessModules"));
    PFN_GetModuleFileNameExA nameOf   = reinterpret_cast<PFN_GetModuleFileNameExA>(GetProcAddress(k32, "K32GetModuleFileNameExA"));
    if (enumMods == nullptr || nameOf == nullptr) { printf("modules: unavailable\n"); return; }
    HMODULE mods[1024]; DWORD bytes = 0;
    if (!enumMods(GetCurrentProcess(), mods, sizeof(mods), &bytes)) { printf("modules: enumeration failed\n"); return; }
    const DWORD n = bytes / sizeof(HMODULE);
    printf("nvidia modules in this process (%lu modules total):\n", static_cast<unsigned long>(n));
    for (DWORD i = 0; i < n && i < 1024; ++i)
    {
        char path[MAX_PATH]; if (nameOf(GetCurrentProcess(), mods[i], path, MAX_PATH) == 0) continue;
        const char *leaf = strrchr(path, '\\'); leaf = leaf ? leaf + 1 : path;
        if (_strnicmp(leaf, "nv", 2) == 0 || _strnicmp(leaf, "_nv", 3) == 0) printf("  %s\n", path);
    }
}


int main(int argc, char **argv)
{
    // Unbuffered, always. When the runner redirects stdout to a file it becomes
    // block-buffered, so a crash loses everything not yet flushed -- and an empty
    // host.out beside a 0xC0000409 reads as "it died before main", which is a wrong
    // conclusion this file's author reached once already.
    setvbuf(stdout, nullptr, _IONBF, 0);

    // An _nvngx.dll beside this host is loaded first, by name, before anything
    // else can pull the driver store's copy in by absolute path. NGX resolves
    // that file by bare name, so the executable's directory wins the search --
    // but only for whoever asks first, and NVIDIA's own loader resolution finds
    // the driver store through the registry. In the scenarios where the
    // substitute contract arms ten seconds in, the driver store's copy was
    // already mapped by then and the staged one lost. Loading it here settles
    // the order. See the note beside the loader staging in run.ps1 for why a
    // loader would be staged at all.
    {
        wchar_t pn[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, pn, MAX_PATH);
        if (wchar_t *e = wcsrchr(pn, L'\\')) wcscpy_s(e + 1, MAX_PATH - (e + 1 - pn), L"_nvngx.dll");
        if (GetFileAttributesW(pn) != INVALID_FILE_ATTRIBUTES)
            printf("_nvngx.dll beside this host: %s\n",
                   LoadLibraryW(pn) != nullptr ? "loaded first, so it wins the name" : "LoadLibrary FAILED");
    }

    // A d3d12.dll beside this host is loaded first, by name. This host never
    // uses D3D12 itself; the file is ReShade under that name, which is how one
    // D3D11 title (Arknights: Endfield, dlss5-bridge #17) carries it: the game
    // imports d3d12.dll, ReShade arrives as that proxy and hooks D3D11 late.
    // Without this load nothing would pull that file in and ReShade would never
    // attach. The runner stages it with -Proxy d3d12.
    {
        wchar_t p12[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, p12, MAX_PATH);
        if (wchar_t *e = wcsrchr(p12, L'\\')) wcscpy_s(e + 1, MAX_PATH - (e + 1 - p12), L"d3d12.dll");
        if (GetFileAttributesW(p12) != INVALID_FILE_ATTRIBUTES)
            printf("d3d12.dll beside this host: %s\n", LoadLibraryW(p12) != nullptr ? "loaded first, as the game would" : "LoadLibrary FAILED");
    }

    Scenario sc = {};
    if (argc > 1 && strstr(argv[1], ".txt") != nullptr)
    {
        if (!ScenarioLoad(&sc, argv[1])) { printf("FAIL: scenario\n"); return 2; }
        if (sc.probe_compute) { puts("FAIL: probecompute is a Vulkan diagnostic"); return 2; }
    }
    else
    {
        // No scenario file: the smoke test, so a bare run still does something.
        int nf = 600;
        if (argc > 1 && (!Num(argv[1], &nf) || nf <= 0)) { printf("FAIL: '%s' is neither a scenario file nor a frame count\n", argv[1]); return 2; }
        ScenarioAdd(&sc, STEP_FRAMES, nf);
        strncpy_s(sc.name, "smoke", _TRUNCATE);
    }
    printf("ngxGym: scenario '%s', %d steps\n", sc.name, sc.count);

    Host h;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ngxGym";
    RegisterClassExW(&wc);
    RECT r = { 0, 0, static_cast<LONG>(h.out_w), static_cast<LONG>(h.out_h) };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    h.hwnd = CreateWindowExW(BackgroundMode() ? (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) : 0, L"ngxGym", L"ngxGym", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (h.hwnd == nullptr) { printf("FAIL: no window\n"); return 2; }
    ShowHostWindow(h.hwnd);

    D3D_FEATURE_LEVEL got = {};
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 want, 2, D3D11_SDK_VERSION, &h.dev, &got, &h.ctx)))
    { printf("FAIL: D3D11CreateDevice\n"); return 2; }
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&h.fac))))
    { printf("FAIL: CreateDXGIFactory2\n"); return 2; }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = h.out_w; sd.Height = h.out_h; sd.Format = h.display_fmt;
    sd.SampleDesc = { 1, 0 }; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (FAILED(h.fac->CreateSwapChainForHwnd(h.dev, h.hwnd, &sd, nullptr, nullptr, &h.sc)))
    { printf("FAIL: CreateSwapChainForHwnd\n"); return 2; }

    wchar_t here[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, here, MAX_PATH);
    if (wchar_t *s = wcsrchr(here, L'\\')) *(s + 1) = 0;
    const wchar_t *paths[1] = { here };
    NVSDK_NGX_FeatureCommonInfo ci = {};
    ci.PathListInfo.Path = paths;
    ci.PathListInfo.Length = 1;

    // A real GUID, 8-4-4-4-12 hex. NGX parses this rather than hashing it, and
    // answers 0xBAD00005 InvalidParameter to a malformed one -- which is worth
    // knowing, because that code usually means something else entirely.
    if (sc.nodlss) printf("nodlss: NGX is not initialised, as in a game without DLSS\n");
    else {
    NVSDK_NGX_Result nr = NVSDK_NGX_D3D11_Init_with_ProjectID(
        "a7d3f0c8-6b21-4e5a-9f14-3c07b1e9d240", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
        here, h.dev, &ci, NVSDK_NGX_Version_API);
    printf("Init_with_ProjectID -> 0x%08X\n", nr);
    if (NVSDK_NGX_FAILED(nr)) { printf("FAIL: NGX init\n"); return 3; }

    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_GetCapabilityParameters(&h.caps)) || !h.caps)
    { printf("FAIL: GetCapabilityParameters\n"); return 3; }
    int avail = 0;
    h.caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
    if (!avail)
    {
        int res = 0; h.caps->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &res);
        printf("FAIL: DLSS unavailable, FeatureInitResult 0x%08X\n", res);
        return 3;
    }
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_AllocateParameters(&h.p)) || !h.p)
    { printf("FAIL: AllocateParameters\n"); return 3; }
    }

    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "scene", nullptr, nullptr,
                          "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "scene", nullptr, nullptr,
                          "ps", "ps_5_0", 0, 0, &psb, &err)))
    { printf("FAIL: shader: %s\n", err ? static_cast<const char *>(err->GetBufferPointer()) : "?"); return 2; }
    h.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &h.vs);
    h.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &h.ps);
    ID3DBlob *display_code = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "display", nullptr, nullptr,
                          "display_ps", "ps_5_0", 0, 0, &display_code, &err)) ||
        FAILED(h.dev->CreatePixelShader(display_code->GetBufferPointer(), display_code->GetBufferSize(), nullptr, &h.display_ps)))
    { puts("FAIL: display shader"); return 2; }
    display_code->Release();

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(CB); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(h.dev->CreateBuffer(&bd, nullptr, &h.cb))) { printf("FAIL: cbuffer\n"); return 2; }
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    h.dev->CreateDepthStencilState(&dsd, &h.dss);

    h.dlss_on = !sc.nodlss;
    h.colour_chart = sc.colour_chart;
    h.scene_pipeline = sc.scene_pipeline;
    h.scene_provider = sc.scene_provider;
    if (!Rebuild(h, "start")) return 3;

    // How many frames the scenario asks for, so a run that ends early is a failure
    // rather than a short green one. Closing the window at frame 5 of 7600 used to
    // abandon the loop with rc still 0, print the counters, print ok and return 0.
    int rc = 0, want_frames = 0;
    for (int i = 0; i < sc.count; ++i)
        if (sc.steps[i].kind == STEP_FRAMES) want_frames += sc.steps[i].a;
    for (int s = 0; s < sc.count && rc == 0; ++s)
    {
        const Step &st = sc.steps[s];
        switch (st.kind)
        {
        case STEP_FRAMES:
            printf("[%d/%d] frames %d\n", s + 1, sc.count, st.a);
            {
                LARGE_INTEGER t0;
                QueryPerformanceCounter(&t0);
                int done = 0;
                for (int i = 0; i < st.a; ++i, ++done)
                    if (!RenderFrame(h)) { s = sc.count; break; }
                PrintStepRate(t0, done);
            }
            break;
        case STEP_MODE:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            if (!ApplyMode(h, static_cast<Mode>(st.a))) rc = 4;
            break;
        case STEP_RESIZE:
            printf("[%d/%d] resize %d %d\n", s + 1, sc.count, st.a, st.b);
            {
                RECT wr = { 0, 0, st.a, st.b };
                AdjustWindowRect(&wr, static_cast<DWORD>(GetWindowLongPtrW(h.hwnd, GWL_STYLE)), FALSE);
                SetWindowPos(h.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                             SWP_NOMOVE | SWP_NOZORDER);
                if (!ResizeToWindow(h, "resize")) rc = 4;
            }
            break;
        case STEP_PRESET:
            printf("[%d/%d] preset %d\n", s + 1, sc.count, st.a);
            h.quality = st.a;
            if (!Rebuild(h, "preset change")) rc = 4;
            break;
        case STEP_RECREATE:
            printf("[%d/%d] recreate at an unchanged shape\n", s + 1, sc.count);
            if (!Rebuild(h, "recreate")) rc = 4;
            break;
        case STEP_DLSS:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            h.dlss_on = st.a != 0;
            // Off: the textures go to the output size and the feature is released
            // (Rebuild creates none while off). On: the feature is built again.
            if (!Rebuild(h, st.a ? "dlss on" : "dlss off")) rc = 4;
            break;
        case STEP_HDR:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            if (!ApplyHdr(h, st.a != 0)) rc = 4;
            break;
        case STEP_SDR:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            if (!ApplySdr(h, st.a != 0)) rc = 4;
            break;
        case STEP_SCRGB:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            if (!ApplyScrgb(h, st.a != 0, st.b)) rc = 4;
            break;
        case STEP_DEPTHCOLOR:
            printf("FAIL: depthcolor is not implemented on the D3D11 host\n");
            rc = 4;
            break;
        case STEP_PAD:
            printf("[%d/%d] pad %d rows\n", s + 1, sc.count, st.a);
            h.pad = static_cast<UINT>(st.a < 0 ? 0 : st.a);
            if (!Rebuild(h, "pad")) rc = 4;
            break;
        case STEP_SUBRECT:
            printf("[%d/%d] subrect %d %d\n", s + 1, sc.count, st.a, st.b);
            h.sub_w = static_cast<UINT>(st.a); h.sub_h = static_cast<UINT>(st.b);
            break;
        case STEP_SUBRECTAT:
            printf("[%d/%d] subrectat %d %d\n", s + 1, sc.count, st.a, st.b);
            h.sub_x = static_cast<UINT>(st.a); h.sub_y = static_cast<UINT>(st.b);
            break;
        case STEP_TRANSPOSE:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            h.transpose = st.a != 0;
            break;
        case STEP_STALE:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            h.stale = st.a != 0;
            break;
        case STEP_OMIT:
            printf("[%d/%d] omit %d\n", s + 1, sc.count, st.a);
            {
                const bool was = h.omit == OMIT_FLAGS;
                h.omit = static_cast<Omit>(st.a);
                // Into or out of "omit flags": the create block changes either
                // way. Leaving it used to keep the flagless feature for the
                // control segments that followed.
                if (was != (st.a == OMIT_FLAGS) && !Rebuild(h, was ? "omit ends" : "omit flags")) rc = 4;
            }
            break;
        case STEP_EXPOSURE:
            printf("[%d/%d] %s\n", s + 1, sc.count, StepName(st));
            h.exposure_on = st.a != 0;
            break;
        // Loud, not silent. A verb the parser accepted and the executor drops is a
        // run that proves something other than what its file says -- which happened
        // here: exposure on/off parsed, printed nothing, changed nothing, and the
        // scenario reported PASS having tested the state it was not testing. A
        // switch over an enum warns about this only at /Wall, so the default does
        // the job instead.
        default:
            printf("[%d/%d] FAIL: step kind %d is parsed but not executed\n",
                   s + 1, sc.count, static_cast<int>(st.kind));
            rc = 5;
            break;
        }
    }

    printf("frames %d, evaluates %d, succeeded %d\n", h.frame, h.evaluated, h.delivered);

    ReleaseFeat(h);
    if (h.p) NVSDK_NGX_D3D11_DestroyParameters(h.p);
    if (h.caps) NVSDK_NGX_D3D11_Shutdown1(h.dev);
    if (h.dss) h.dss->Release(); if (h.cb) h.cb->Release();
    if (h.vs) h.vs->Release();   if (h.ps) h.ps->Release();
    if (h.display_ps) h.display_ps->Release();
    if (vsb) vsb->Release();     if (psb) psb->Release();
    h.color.Release(); h.mv.Release(); h.depth.Release(); h.output.Release();
    h.exposure.Release();
    BOOL fs = FALSE;
    if (SUCCEEDED(h.sc->GetFullscreenState(&fs, nullptr)) && fs) h.sc->SetFullscreenState(FALSE, nullptr);
    h.sc->Release(); h.fac->Release(); h.ctx->Release(); h.dev->Release();
    DestroyWindow(h.hwnd);

    if (h.frame < want_frames)
    { printf("FAIL: ran %d of %d frames the scenario asked for\n", h.frame, want_frames); return 6; }
    PrintNvModules();
    if (rc != 0) { printf("FAIL: scenario stopped\n"); return rc; }
    if (h.evaluated > 0 && h.delivered == 0) { printf("FAIL: no evaluate succeeded\n"); return 4; }
    printf("ok\n");
    return 0;
}
