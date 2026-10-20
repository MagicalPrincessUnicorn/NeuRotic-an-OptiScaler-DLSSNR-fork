// Adapted from DLSS5 Feeder v1.18.0-beta.1; Copyright (c) 2026 Jean-Laurent ROUZIES.
// MIT: ../LICENSE-DLSS5-FEEDER.txt. Provider shaders are separately installed.
// Host must update inputs immediately BEFORE this technique every runtime frame.
// Read NRPG_RuntimeFrame from the runtime and echo it into NRPG_HostFrame.
// Capture/PreviousCapture/Generation are bounded integers [1,16777214]; rotate the
// episode before this bound. Increment Generation on reload/resize/provider change/cut.
// Set Reset=true on the first capture, cuts and any broken pair; then explicitly clear it.
// HistoryValid certifies that PreviousCapture is the estimator's actual reference.
// MotionReady/DepthReady certify producer execution, source association and descriptors.
// Host absence or stale uniforms produce zero motion and full distrust, never valid history.
// Outputs: NRPG_Motion RG16F full-resolution pixels, current -> previous;
// NRPG_RawDepth R32F raw corrected hardware depth; NRPG_Distrust R8 [0,1].
// NRPG_Status RGBA8: R=reason bits /255; G=motion usable; B=depth usable; A=history usable.
// Reason bits: 1 host stale, 2 history invalid/reset, 4 invalid provider/depth,
// 8 appearance, 16 depth disagreement, 32 flow consistency, 64 accepted static, 128 outside.
// Status validity does not grant resource rights or certify engine-native motion.
#include "ReShade.fxh"
#if __RENDERER__ >= 0xA000
uniform uint NRPG_RuntimeFrame < source = "framecount"; >;
uniform bool NRPG_HostEnabled = false;
uniform bool NRPG_HistoryValid = false;
uniform bool NRPG_Reset = true;
uniform bool NRPG_MotionReady = false;
uniform bool NRPG_DepthReady = false;
uniform uint NRPG_HostFrame = 0;
uniform uint NRPG_Capture = 0;
uniform uint NRPG_PreviousCapture = 0;
uniform uint NRPG_Generation = 0;
texture NRPG_ColorInput : COLOR;
sampler sNRPG_ColorInput { Texture = NRPG_ColorInput; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
#ifndef NRPG_MV_PROVIDER
    #define NRPG_MV_PROVIDER 0
#endif

// ---------------------------------------------------------------------------------------------
// The selected provider's output, declared byte for byte like the provider itself does.
// ---------------------------------------------------------------------------------------------

#if NRPG_MV_PROVIDER == 1
    // iMMERSE Launchpad (MartysMods/mmx_deferred.fxh)
    namespace Deferred {
        texture MotionVectorsTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RG16F; };
        // Launchpad's request buffer. Launchpad only computes optical flow when a consumer asked
        // for it during the previous frame (it reads this 1x1 RGBA8 at the top of its technique
        // and clears it at the bottom; bit 4 = optical flow, written through the render-target
        // write mask). Being below Launchpad in the list, our request lands for the next frame.
        // Declared like Launchpad declares it; the two shaders that write it below are ours.
        namespace IPC {
            texture2D PredicationBuffer { Format = RGBA8; };
        }
    }
    sampler sNRPG_ProviderMV { Texture = Deferred::MotionVectorsTex; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
    float4 NRPG_IpcRequestVS(in uint id : SV_VertexID) : SV_Position { return float4(0.0, 0.0, 0.0, 1.0); }
    float4 NRPG_IpcRequestPS(in float4 vpos : SV_Position) : SV_Target0 { return 1.0; }
    #define NRPG_MV_PROVIDER_NAME "Launchpad (Deferred::MotionVectorsTex)"
    #define NRPG_MV_REQUEST_PASS pass IpcRequestOpticalFlow { PrimitiveTopology = POINTLIST; VertexCount = 1; VertexShader = NRPG_IpcRequestVS; PixelShader = NRPG_IpcRequestPS; RenderTarget = Deferred::IPC::PredicationBuffer; RenderTargetWriteMask = 4; }
#elif NRPG_MV_PROVIDER == 2
    // VORT (Includes/vort_MotionUtils.fxh, V_MV_MODE 1)
    texture2D MotVectTexVort { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RG16F; };
    sampler sNRPG_ProviderMV { Texture = MotVectTexVort; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
    #define NRPG_MV_PROVIDER_NAME "VORT (MotVectTexVort)"
#elif NRPG_MV_PROVIDER == 3
    // LumeniteFX Kernel (lumenite_Kernel.fx), as lumenite_RTAO/TRAA re-declare it. 1/8 resolution.
    namespace Kernel {
        texture2D tFlow { Width = BUFFER_WIDTH/8; Height = BUFFER_HEIGHT/8; Format = RG16F; };
        texture2D tConfidence { Width = BUFFER_WIDTH/8; Height = BUFFER_HEIGHT/8; Format = R16F; };
    }
    sampler sNRPG_ProviderMV        { Texture = Kernel::tFlow;       AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Linear; MagFilter = Linear; };
    sampler sNRPG_ProviderMVPoint   { Texture = Kernel::tFlow;       AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point;  MagFilter = Point;  };
    sampler sNRPG_ProviderConfidence{ Texture = Kernel::tConfidence; AddressU = Clamp; AddressV = Clamp; };
    #define NRPG_MV_PROVIDER_NAME "LumeniteFX Kernel (Kernel::tFlow, 1/8 res)"
    #define NRPG_MV_LOWRES 1
#elif NRPG_MV_PROVIDER == 4
    // LumeniteFX QuantMotion (lumenite_QuantMotion.fx), as lumenite_QuantAO re-declares it. 1/8 resolution.
    namespace QuantMotion {
        texture2D tFlow { Width = BUFFER_WIDTH/8; Height = BUFFER_HEIGHT/8; Format = RG16F; };
        texture2D tConfidence { Width = BUFFER_WIDTH/8; Height = BUFFER_HEIGHT/8; Format = R16F; };
    }
    sampler sNRPG_ProviderMV        { Texture = QuantMotion::tFlow;       AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Linear; MagFilter = Linear; };
    sampler sNRPG_ProviderMVPoint   { Texture = QuantMotion::tFlow;       AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point;  MagFilter = Point;  };
    sampler sNRPG_ProviderConfidence{ Texture = QuantMotion::tConfidence; AddressU = Clamp; AddressV = Clamp; };
    #define NRPG_MV_PROVIDER_NAME "LumeniteFX QuantMotion (QuantMotion::tFlow, 1/8 res)"
    #define NRPG_MV_LOWRES 1
#elif NRPG_MV_PROVIDER == 5
    // Owned built-in software/NVIDIA flow bound only after producer GPU completion.
    texture NRPG_BuiltinMotion : NRPG_BUILTIN_MOTION;
    texture NRPG_BuiltinDistrust : NRPG_BUILTIN_DISTRUST;
    sampler sNRPG_ProviderMV { Texture = NRPG_BuiltinMotion; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
    sampler sNRPG_BuiltinDistrust { Texture = NRPG_BuiltinDistrust; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
    #define NRPG_MV_PROVIDER_NAME "NeuRotic owned software/hardware flow"
#else
    // The community-standard shared texture (ReshadeMotionEstimation, qUINT, dh_uber_motion, ...)
    texture texMotionVectors < pooled = false; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RG16F; };
    sampler sNRPG_ProviderMV { Texture = texMotionVectors; AddressU = Clamp; AddressV = Clamp; MipFilter = Point; MinFilter = Point; MagFilter = Point; };
    #define NRPG_MV_PROVIDER_NAME "texMotionVectors (DRME, qUINT, dh_uber_motion, ...)"
#endif

#ifndef NRPG_MV_LOWRES
    #define NRPG_MV_LOWRES 0
#endif
#ifndef NRPG_MV_REQUEST_PASS
    #define NRPG_MV_REQUEST_PASS
#endif

// ---------------------------------------------------------------------------------------------

uniform int MV_PROVIDER_INFO <
    ui_type = "radio";
    ui_label = " ";
    ui_text = "Motion vector provider: " NRPG_MV_PROVIDER_NAME "\n"
              "Change it with the NRPG_MV_PROVIDER preprocessor definition:\n"
              "  0 texMotionVectors (DRME, qUINT, dh_uber_motion)   1 Launchpad   2 VORT\n"
              "  3 LumeniteFX Kernel   4 LumeniteFX QuantMotion\n"
              "Enable that provider's technique ABOVE DLSS 5 Feed.";
>;

#if NRPG_MV_LOWRES
uniform int MV_LOWRES_FILTER <
    ui_type = "combo";
    ui_items = "Bilinear\0Point (nearest)\0";
    ui_label = "Low-res provider filter";
    ui_tooltip = "How the provider's 1/8-resolution flow is brought up to full resolution.\n"
                 "Bilinear smooths across flow cells; point keeps each 8x8 cell's vector as-is.";
> = 0;
#endif

uniform bool MV_VALIDATE <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Validate motion vectors against the previous frame";
    ui_tooltip = "Optical-flow providers answer a lighting change (flicker, flames) with a vector that\n"
                 "points at whatever happened to match. Reprojecting and checking catches those:\n"
                 "the vector is zeroed and DLSS is told to trust the current frame there (NRPG_Distrust).";
> = true;

uniform bool VALIDATE_STATIC <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Static-hypothesis test (zeroes the vector, keeps history)";
    ui_tooltip = "For each pixel, asks which explains it better: 'did not move' or the provider's vector.\n"
                 "Both are scored on illumination-normalised 3x3 structure (local mean removed), so a\n"
                 "flickering light does not count as motion. When 'did not move' wins, the vector is zeroed\n"
                 "and the pixel is NOT masked -- a static wall wants its full history, which is what smooths\n"
                 "the flicker. This is the test for the flickering-wall case.";
> = true;

uniform bool STATIC_HYSTERESIS <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Static test: require two frames in a row";
    ui_tooltip = "The static test has no memory: on a low-contrast surface under a slow pan it can win on\n"
                 "one frame and lose on the next, so the vector alternates between the provider's and zero\n"
                 "and DLSS alternately reprojects and does not -- a flicker/judder that comes and goes.\n"
                 "With this on, the vector is only zeroed where the test won on this frame AND the last;\n"
                 "on the first frame the provider's vector is kept and the pixel is masked instead, so\n"
                 "DLSS leans on the current frame rather than reprojecting from nowhere.\n"
                 "Turn it off to compare against the old (per-frame) behaviour.";
> = true;

uniform float STATIC_BIAS <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_label = "Static bias";
    ui_tooltip = "How much worse (relative) the static explanation may score than the vector's and still win.\n"
                 "0 = the vector must strictly beat 'did not move'. Higher favours zero vectors.";
> = 0.15;

uniform float STATIC_MIN_CONTRAST <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 0.1; ui_step = 0.001;
    ui_label = "Static test: minimum patch contrast";
    ui_tooltip = "Below this 3x3 contrast (mean absolute deviation of luma) a patch has no structure to judge\n"
                 "motion by, and the test abstains -- the provider's vector stands. Raise it if flat surfaces\n"
                 "trail while moving (yellow on plain motion in the debug view); lower it if the\n"
                 "flickering wall stops being caught.";
> = 0.012;

uniform bool VALIDATE_LUMA <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Luma test (mask only)";
    ui_tooltip = "The reprojected previous luma must fall inside the current 3x3 neighbourhood's luma range.\n"
                 "A failure only raises the mask (DLSS leans on the current frame); it never zeroes the vector,\n"
                 "because a lighting change does not prove the surface did not move. Off by default: on a\n"
                 "flickering surface it asks DLSS to drop exactly the history that would smooth the flicker.";
> = false;

uniform float LUMA_TOLERANCE <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_label = "Luma tolerance";
    ui_tooltip = "How far outside the current 3x3 neighbourhood's luma range the reprojected previous luma\n"
                 "may fall (relative to that range's maximum). Lower = stricter.";
> = 0.25;

uniform bool VALIDATE_DEPTH <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Depth test (zeroes the vector)";
    ui_tooltip = "The reprojected previous linear depth must match the current one: a mismatch means the vector\n"
                 "points at a different surface (disocclusion), so it is zeroed and masked. Sky is exempt.";
> = true;

uniform float DEPTH_TOLERANCE <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 0.5; ui_step = 0.005;
    ui_label = "Depth tolerance";
    ui_tooltip = "Allowed relative difference between the reprojected previous linear depth and the current one.";
> = 0.10;

uniform bool VALIDATE_MV <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_label = "Consistency test (zeroes the vector)";
    ui_tooltip = "This frame's vector must resemble the previous frame's vector at the spot it points to.\n"
                 "Real motion is smooth frame to frame; optical flow on fire, smoke or a flickering wall is not.\n"
                 "A failure zeroes the vector and masks the pixel.";
> = true;

uniform float MV_CONSISTENCY <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 16.0; ui_step = 0.1;
    ui_label = "Vector consistency (px)";
    ui_tooltip = "Allowed change, in pixels, between this frame's vector and the previous frame's vector at\n"
                 "the reprojected spot, plus 50% of the vector length. Raise it if plain camera motion\n"
                 "shows blue in the 'Validation tests' debug view.";
> = 1.4;

uniform float MASK_STRENGTH <
    ui_category = "Validation (flicker / flames / disocclusion)";
    ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
    ui_label = "Bias-current-colour mask strength";
    ui_tooltip = "How strongly a distrusted pixel asks DLSS to favour the current frame (NRPG_Distrust).\n"
                 "1 = fully; 0 = only zero the vector, do not mask.";
> = 1.0;

texture NRPG_Motion { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RG16F; };
texture NRPG_RawDepth { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R32F; };
texture NRPG_Distrust { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
texture NRPG_Status { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
texture NRPG_HistoryStamp { Width = 1; Height = 1; Format = RGBA32F; };
sampler sNRPG_HistoryStamp { Texture = NRPG_HistoryStamp; MinFilter = Point; MagFilter = Point; MipFilter = Point; };
// Previous-frame history for validation (written at the end of the technique)
texture NRPG_PrevLuma  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R16F;  };
texture NRPG_PrevDepth { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R16F;  };
texture NRPG_PrevMV    { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RG16F; };
// Luma may be interpolated (a smooth quantity); depth and vectors must NOT be -- bilinear
// across an object edge mixes two surfaces' values and fails the test on every edge in motion.
sampler sNRPG_PrevLuma  { Texture = NRPG_PrevLuma;  AddressU = Clamp; AddressV = Clamp; MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = POINT; };
sampler sNRPG_PrevDepth { Texture = NRPG_PrevDepth; AddressU = Clamp; AddressV = Clamp; MinFilter = POINT;  MagFilter = POINT;  MipFilter = POINT; };
sampler sNRPG_PrevMV    { Texture = NRPG_PrevMV;    AddressU = Clamp; AddressV = Clamp; MinFilter = POINT;  MagFilter = POINT;  MipFilter = POINT; };

// The static-hypothesis decision, this frame and last. The test has no memory of its own,
// so on a low-contrast surface under a slow pan it can win on one frame and lose on the
// next; the vector then alternates between the provider's and zero, and DLSS alternately
// reprojects and does not. That is a flicker/judder that no consumer can smooth out, and
// it comes and goes with the surface (The Surge 2, 2026-09-02). Guides writes StaticNow,
// History copies it into PrevStatic, and the next frame's Guides reads that -- a texture
// cannot be sampled and written in the same pass, which is why it takes two of them.
texture NRPG_StaticNow  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
texture NRPG_PrevStatic { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
sampler sNRPG_StaticNow  { Texture = NRPG_StaticNow;  AddressU = Clamp; AddressV = Clamp; MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; };
sampler sNRPG_PrevStatic { Texture = NRPG_PrevStatic; AddressU = Clamp; AddressV = Clamp; MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; };

// The selected provider's vector at uv, as delta UV (prev_uv = uv + mv).
float2 ProviderMV(float2 uv)
{
    float4 c = float4(uv, 0.0, 0.0);
#if NRPG_MV_PROVIDER == 5
    return tex2Dlod(sNRPG_ProviderMV, c).xy * BUFFER_PIXEL_SIZE;
#elif NRPG_MV_LOWRES
    return MV_LOWRES_FILTER == 0 ? tex2Dlod(sNRPG_ProviderMV, c).xy : tex2Dlod(sNRPG_ProviderMVPoint, c).xy;
#else
    return tex2Dlod(sNRPG_ProviderMV, c).xy;
#endif
}

float Luma(float2 uv)
{
    return dot(tex2Dlod(sNRPG_ColorInput, float4(uv, 0.0, 0.0)).rgb, float3(0.299, 0.587, 0.114));
}

// Illumination-normalised 3x3 structure difference between the current frame at uv_cur and
// the previous frame at uv_prev: each patch has its own mean removed first, so a brightness
// change (flicker) contributes nothing and only the pattern is compared.
// Also returns the current patch's contrast (mean absolute deviation): a patch with no
// structure cannot decide anything, and the caller must not pretend it can.
float PatchError(float2 uv_cur, float2 uv_prev, out float contrast)
{
    const float2 px = BUFFER_PIXEL_SIZE;
    float c[9], p[9];
    float mc = 0.0, mp = 0.0;
    [unroll] for (int i = 0; i < 9; ++i)
    {
        const float2 o = float2(i % 3 - 1, i / 3 - 1) * px;
        c[i] = Luma(uv_cur + o);
        p[i] = tex2Dlod(sNRPG_PrevLuma, float4(uv_prev + o, 0.0, 0.0)).x;
        mc += c[i]; mp += p[i];
    }
    mc /= 9.0; mp /= 9.0;
    float err = 0.0;
    contrast = 0.0;
    [unroll] for (int j = 0; j < 9; ++j)
    {
        err += abs((c[j] - mc) - (p[j] - mp));
        contrast += abs(c[j] - mc);
    }
    contrast /= 9.0;
    return err / 9.0;
}

// Per-test failure (0 = fine, 1 = failed, soft in between): x = luma, y = depth, z = consistency,
// w = the static hypothesis won. Luma failing says "this pixel's appearance changed"; depth or
// consistency failing says "this vector points at the wrong thing"; static winning says "no
// vector explains this pixel better than zero". Only y, z and w justify zeroing the vector, and
// only x, y, z justify asking DLSS to distrust history.
float4 ValidateTests(float2 uv, float2 mv)
{
    const float2 puv = uv + mv;
    float4 bad = 0.0;
    // Reprojecting off-screen: nothing to compare against. Keep the vector (DLSS handles
    // it) and let the mask lean on the current frame.
    if (any(puv < 0.0) || any(puv > 1.0)) return float4(1.0, 0.0, 0.0, 0.0);

    // 0. Static hypothesis: does "did not move" explain this pixel at least as well as the
    //    vector does? Scored on mean-removed structure, so flicker is not motion. Skipped for
    //    vectors under half a pixel (nothing to decide).
    if (VALIDATE_STATIC && length(mv * BUFFER_SCREEN_SIZE) > 0.5)
    {
        float sc, unused;
        const float es = PatchError(uv, uv, sc);
        const float ef = PatchError(uv, puv, unused);
        // Only a patch with structure can tell the two apart. Below the contrast floor the
        // scores tie for lack of evidence, and a tie must go to the provider (its flow is
        // propagated from textured neighbours -- the right guess for a moving flat wall).
        // With structure, static wins only if it beats the vector by a share of that contrast.
        if (sc >= STATIC_MIN_CONTRAST)
            bad.w = es + 0.25 * sc <= ef * (1.0 + STATIC_BIAS) ? 1.0 : 0.0;
    }

    // 1. Luma: current 3x3 range vs the previous luma at the reprojected spot.
    if (VALIDATE_LUMA)
    {
        const float2 px = BUFFER_PIXEL_SIZE;
        float lc = Luma(uv), lmin = lc, lmax = lc;
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float l = Luma(uv + float2(x, y) * px);
            lmin = min(lmin, l); lmax = max(lmax, l);
        }
        const float lp     = tex2Dlod(sNRPG_PrevLuma, float4(puv, 0.0, 0.0)).x;
        const float margin = LUMA_TOLERANCE * max(lmax, 0.05) + 2.0 / 255.0;
        bad.x = saturate(max(lmin - lp, lp - lmax) / margin);
    }

    // 2. Depth: previous linear depth at the reprojected spot vs the current one (sky exempt).
    const float dc = ReShade::GetLinearizedDepth(uv);
    if (VALIDATE_DEPTH && dc < 0.999)
    {
        const float dp  = tex2Dlod(sNRPG_PrevDepth, float4(puv, 0.0, 0.0)).x;
        const float tol = DEPTH_TOLERANCE * max(dc, 1e-3);
        bad.y = saturate((abs(dp - dc) - tol) / (tol + 1e-5));
    }

    // 3. Consistency: the previous frame's vector where this pixel came from vs this one.
    if (VALIDATE_MV && MV_CONSISTENCY > 0.0)
    {
        const float2 pmv   = tex2Dlod(sNRPG_PrevMV, float4(puv, 0.0, 0.0)).xy;
        const float  diff  = length((mv - pmv) * BUFFER_SCREEN_SIZE);
        const float  allow = MV_CONSISTENCY + 0.5 * length(mv * BUFFER_SCREEN_SIZE);
        bad.z = saturate((diff - allow) / allow);
    }
    return bad;
}

float RawDepth(float2 uv)
{
    // Raw hardware depth, exactly as the game wrote it -- the same orientation/offset
    // corrections ReShade.fxh applies in GetLinearizedDepth(), minus the linearisation
    // (DLSS must receive the raw values; the add-on tells it whether the range is reversed).
    float2 t = uv;
#if RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
    t.y = 1.0 - t.y;
#endif
    t.x /= RESHADE_DEPTH_INPUT_X_SCALE;
    t.y /= RESHADE_DEPTH_INPUT_Y_SCALE;
#if RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET
    t.x -= RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET * BUFFER_RCP_WIDTH;
#else
    t.x -= RESHADE_DEPTH_INPUT_X_OFFSET / 2.000000001;
#endif
#if RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET
    t.y += RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET * BUFFER_RCP_HEIGHT;
#else
    t.y += RESHADE_DEPTH_INPUT_Y_OFFSET / 2.000000001;
#endif
    return tex2Dlod(ReShade::DepthBuffer, float4(t, 0.0, 0.0)).x;
}


bool HostFresh()
{
    return NRPG_HostEnabled && NRPG_HostFrame == NRPG_RuntimeFrame &&
        NRPG_RuntimeFrame <= 16777214u && NRPG_Capture > 0u && NRPG_Capture <= 16777214u &&
        NRPG_Generation > 0u && NRPG_Generation <= 16777214u;
}
bool HistoryUsable()
{
    float4 stamp = tex2Dlod(sNRPG_HistoryStamp, float4(0.5, 0.5, 0, 0));
    return HostFresh() && !NRPG_Reset && NRPG_HistoryValid &&
        NRPG_PreviousCapture > 0u && NRPG_PreviousCapture != NRPG_Capture &&
        stamp.x == float(NRPG_PreviousCapture) && stamp.y == float(NRPG_Generation) &&
        stamp.z + 1.0 == float(NRPG_RuntimeFrame) && stamp.w == 1.0;
}
bool FlowFinite(float2 flow)
{
    // Ordered comparisons reject NaN as well as infinity and half-float overflow.
    return all(abs(flow * float2(BUFFER_WIDTH, BUFFER_HEIGHT)) < 32752.0);
}
void PS_Prepare(float4 vpos : SV_Position, float2 uv : TEXCOORD,
    out float2 motion : SV_Target0, out float distrust : SV_Target1,
    out float depth : SV_Target2, out float static_now : SV_Target3,
    out float4 status : SV_Target4)
{
    bool fresh = HostFresh();
    bool history_ok = HistoryUsable();
    float2 flow = ProviderMV(uv);
    bool provider_ok = fresh && NRPG_MotionReady && FlowFinite(flow);
    depth = RawDepth(uv);
    bool depth_ok = fresh && NRPG_DepthReady && depth >= 0.0 && depth <= 1.0;
    if (!(depth >= 0.0 && depth <= 1.0)) depth = 1.0;
    uint reasons = !fresh ? 1u : 0u;
    if (!provider_ok || !depth_ok) reasons |= 4u;
    float2 mv = float2(0.0, 0.0);
    distrust = 0.0;
    static_now = 0.0;
    if (!history_ok)
    {
        reasons |= 2u;
        distrust = 1.0;
    }
    else if (!provider_ok)
    {
        distrust = 1.0;
    }
    else
    {
        mv = flow;
        if (MV_VALIDATE)
        {
            float4 bad = ValidateTests(uv, flow);
            if (!depth_ok) bad.y = 0.0; // unknown depth cannot establish a mismatch
            static_now = bad.w;
            float static_zero = bad.w;
            if (STATIC_HYSTERESIS && bad.w > 0.5)
            {
                float won_before = tex2Dlod(sNRPG_PrevStatic, float4(uv, 0, 0)).x;
                if (won_before <= 0.5) { static_zero = 0.0; distrust = 0.35; }
            }
            bool zero_vector = max(bad.y, max(bad.z, static_zero)) > 0.5;
            distrust = max(distrust, max(bad.x, max(bad.y, bad.z))) * MASK_STRENGTH;
            mv = zero_vector ? float2(0.0, 0.0) : flow;
            if (bad.x > 0.0) reasons |= 8u;
            if (bad.y > 0.0) reasons |= 16u;
            if (bad.z > 0.0) reasons |= 32u;
            if (static_zero > 0.5) reasons |= 64u;
        }
        if (any(uv + flow < 0.0) || any(uv + flow > 1.0)) { reasons |= 128u; distrust = 1.0; }
    }
#if NRPG_MV_PROVIDER == 5
    distrust = max(distrust, tex2Dlod(sNRPG_BuiltinDistrust, float4(uv, 0.0, 0.0)).x);
#endif
    if (!depth_ok) distrust = 1.0;
    motion = mv * float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    status = float4(float(reasons) / 255.0, provider_ok && history_ok ? 1.0 : 0.0,
        depth_ok ? 1.0 : 0.0, history_ok ? 1.0 : 0.0);
}
void PS_StoreHistory(float4 vpos : SV_Position, float2 uv : TEXCOORD,
    out float luma : SV_Target0, out float depth : SV_Target1,
    out float2 mv : SV_Target2, out float previous_static : SV_Target3)
{
    luma = Luma(uv);
    depth = ReShade::GetLinearizedDepth(uv);
    float2 raw = ProviderMV(uv);
    mv = FlowFinite(raw) ? raw : float2(0.0, 0.0);
    previous_static = tex2Dfetch(sNRPG_StaticNow, int2(vpos.xy)).x;
}
float4 PS_StoreStamp(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return HostFresh() ? float4(float(NRPG_Capture), float(NRPG_Generation),
        float(NRPG_RuntimeFrame), NRPG_MotionReady && NRPG_DepthReady ? 1.0 : 0.0) : 0.0;
}
technique NeuRotic_PreparedGuides < ui_label = "NeuRotic prepared guides (after motion provider)"; >
{
    pass Guides { VertexShader = PostProcessVS; PixelShader = PS_Prepare;
        RenderTarget0 = NRPG_Motion; RenderTarget1 = NRPG_Distrust;
        RenderTarget2 = NRPG_RawDepth; RenderTarget3 = NRPG_StaticNow; RenderTarget4 = NRPG_Status; }
    pass History { VertexShader = PostProcessVS; PixelShader = PS_StoreHistory;
        RenderTarget0 = NRPG_PrevLuma; RenderTarget1 = NRPG_PrevDepth;
        RenderTarget2 = NRPG_PrevMV; RenderTarget3 = NRPG_PrevStatic; }
    pass Stamp { VertexShader = PostProcessVS; PixelShader = PS_StoreStamp; RenderTarget = NRPG_HistoryStamp; }
    NRPG_MV_REQUEST_PASS
}
#endif
