// Adapted from DLSS5 Feeder v1.18.0-beta.1; Copyright (c) 2026 Jean-Laurent ROUZIES.
// MIT: ../LICENSE-DLSS5-FEEDER.txt. Optional SDR experiment; no HDR qualification.
// Source-guided Color Refinement B++ -- local AI-assisted prototype.
// Required order: NeuRotic_SourceGuided_Save -> NeuRotic_PreparedGuides -> NeuRotic_SourceGuided_Apply.
// No closed NR changes, extra NGX evaluation, fixed UI colours or screen boxes.
// Oklab coefficients: https://bottosson.github.io/posts/oklab/ (public domain).
// FX resource/sampler/pass semantics:
// https://github.com/crosire/reshade-shaders/blob/slim/REFERENCE.md
// Upstream compiler target: ReShade 6.8.0, commit 18deaa52de0c425a78b329e9cb3c497281cd00ec.
// Compile-off removes ALL resources and passes. Runtime bypass retains their cost.
#ifndef NEUROTIC_SOURCE_GUIDED_COLOR
#define NEUROTIC_SOURCE_GUIDED_COLOR 0
#endif

#if NEUROTIC_SOURCE_GUIDED_COLOR == 1 && __RENDERER__ >= 0xb000 && BUFFER_COLOR_SPACE == 1 && BUFFER_COLOR_BIT_DEPTH == 8

uniform uint NRSG_Frame < source = "framecount"; >;
uniform bool NRSG_HostEnabled = false;
uniform uint NRSG_HostFrame = 0;
uniform uint NRSG_Capture = 0;
uniform uint NRSG_ProcessedCapture = 0;
uniform uint NRSG_Generation = 0;
uniform uint NRSG_ProcessedGeneration = 0;
bool NRSG_HostFresh() {
    return NRSG_HostEnabled && NRSG_HostFrame == NRSG_Frame && NRSG_Frame <= 16777214u &&
        NRSG_Capture > 0u && NRSG_Capture <= 16777214u &&
        NRSG_Generation > 0u && NRSG_Generation <= 16777214u;
}
uniform bool NRSG_Bypass < source = "key"; keycode = 118; mode = "toggle"; >;
uniform bool NRSG_Enabled <
    ui_label = "Enable source-guided colours";
    ui_tooltip = "SDR sRGB prototype. F7 temporarily bypasses refinement only; F6 remains NR. Both techniques must surround NeuRotic_PreparedGuides.";
> = false;
uniform float NRSG_Strength <
    ui_type = "slider"; ui_label = "Colour separation";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_tooltip = "0: neural output. 0.90: selected B++. Can also attenuate useful broad NR colour changes.";
> = 0.9;
uniform float NRSG_Tone <
    ui_type = "slider"; ui_label = "Tone and contrast correction";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_tooltip = "Affects the lightness base of every colour, not just white text.";
> = 1.0;
uniform float NRSG_Detail <
    ui_type = "slider"; ui_label = "Neural residual detail";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
    ui_tooltip = "1.00 preserves the neural residual. Larger values can amplify noise and halos.";
> = 1.0;
uniform int NRSG_View <
    ui_type = "combo"; ui_label = "Comparison view";
    ui_items = "Processed\0Source before Feeder\0Neural / post Feeder\0";
> = 0;

texture NeuRotic_SourceGuided_Color : COLOR;
texture NeuRotic_SourceGuided_SourceLab { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F; };
texture NeuRotic_SourceGuided_NeuralLab { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA32F; };
texture NRSG_SourceStamp { Width = 1; Height = 1; Format = RGBA32F; };
sampler NRSG_Stamp {
    Texture = NRSG_SourceStamp; MinFilter = Point; MagFilter = Point; MipFilter = Point;
};
sampler NRSG_Color {
    Texture = NeuRotic_SourceGuided_Color; AddressU = Clamp; AddressV = Clamp;
    MinFilter = Point; MagFilter = Point; MipFilter = Point; SRGBTexture = false;
};
sampler NRSG_Source {
    Texture = NeuRotic_SourceGuided_SourceLab; AddressU = Clamp; AddressV = Clamp;
    MinFilter = Point; MagFilter = Point; MipFilter = Point; SRGBTexture = false;
};
sampler NRSG_Neural {
    Texture = NeuRotic_SourceGuided_NeuralLab; AddressU = Clamp; AddressV = Clamp;
    MinFilter = Point; MagFilter = Point; MipFilter = Point; SRGBTexture = false;
};

void NRSG_VS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD) {
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 NRSG_ToLab(float3 rgb) {
    // Encoded sRGB is converted exactly once; samplers and writes are non-sRGB.
    float3 hi = pow((saturate(rgb) + 0.055) / 1.055, 2.4);
    float3 lin = float3(rgb.r <= 0.04045 ? rgb.r/12.92 : hi.r,
                        rgb.g <= 0.04045 ? rgb.g/12.92 : hi.g,
                        rgb.b <= 0.04045 ? rgb.b/12.92 : hi.b);
    float3 lms = float3(dot(lin, float3(.4122214708, .5363325363, .0514459929)),
                        dot(lin, float3(.2119034982, .6806995451, .1073969566)),
                        dot(lin, float3(.0883024619, .2817188376, .6299787005)));
    lms = pow(max(lms, 0.0), 1.0/3.0);
    return float3(dot(lms, float3(.2104542553, .7936177850, -.0040720468)),
                  dot(lms, float3(1.9779984951, -2.4285922050, .4505937099)),
                  dot(lms, float3(.0259040371, .7827717662, -.8086757660)));
}

float3 NRSG_ToLinear(float3 lab) {
    // Inverses of the exact matrices used by the existing CPU reference.
    float3 lms = float3(dot(lab, float3(.999999998450520, .396337792173768, .215803758060759)),
                        dot(lab, float3(1.000000008881761, -.105561342323656, -.063854174771706)),
                        dot(lab, float3(1.000000054672411, -.089484182094966, -1.291485537864092)));
    lms = lms*lms*lms;
    return float3(dot(lms, float3(4.076741661347994, -3.307711590408193, .230969928729428)),
                  dot(lms, float3(-1.268438004092176, 2.609757400663371, -.341319396310220)),
                  dot(lms, float3(-.004196086541837, -.703418614459450, 1.707614700930945)));
}

float3 NRSG_Encode(float3 lin) {
    lin = saturate(lin);
    float3 hi = 1.055 * pow(lin, 1.0/2.4) - .055;
    return float3(lin.r <= .0031308 ? lin.r*12.92 : hi.r,
                  lin.g <= .0031308 ? lin.g*12.92 : hi.g,
                  lin.b <= .0031308 ? lin.b*12.92 : hi.b);
}

bool NRSG_Fits(float3 lin) {
    return all(lin >= -1e-9) && all(lin <= 1.0 + 1e-9);
}

float3 NRSG_Compress(float3 lab) {
    lab.x = saturate(lab.x);
    float3 lin = NRSG_ToLinear(lab);
    if (NRSG_Fits(lin)) return NRSG_Encode(lin);
    float low = 0.0, high = 1.0;
    [loop] for (int i = 0; i < 16; ++i) {
        float mid = (low + high) * .5;
        if (NRSG_Fits(NRSG_ToLinear(float3(lab.x, lab.yz * mid)))) low = mid;
        else high = mid;
    }
    return NRSG_Encode(NRSG_ToLinear(float3(lab.x, lab.yz * low)));
}

float4 NRSG_SavePS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    // A zero/uninitialised target is invalid. Exact float32 integer stamps avoid
    // modulo wrap accepting a stale source. Beyond this range, fail closed.
    float stamp = NRSG_HostFresh() ? float(NRSG_Capture) : 0.0;
    return float4(NRSG_ToLab(tex2D(NRSG_Color, uv).rgb), stamp);
}

float4 NRSG_NeuralPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    float4 neural = tex2D(NRSG_Color, uv);
    return float4(NRSG_ToLab(neural.rgb), neural.a);
}

float4 NRSG_StampPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    return NRSG_HostFresh() ? float4(float(NRSG_Capture), float(NRSG_Generation),
        float(NRSG_Frame), 1.0) : 0.0;
}

float4 NRSG_ApplyPS(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    float4 originalNeural = tex2D(NRSG_Color, uv);
    if (!NRSG_Enabled || NRSG_Bypass || NRSG_View == 2 ||
        (NRSG_Strength <= 0.0 && NRSG_View != 1)) return originalNeural;
    float4 source = tex2D(NRSG_Source, uv);
    float4 stamp = tex2Dlod(NRSG_Stamp, float4(0.5, 0.5, 0.0, 0.0));
    if (!NRSG_HostFresh() || NRSG_ProcessedCapture != NRSG_Capture ||
        NRSG_ProcessedGeneration != NRSG_Generation || source.a != float(NRSG_Capture) ||
        stamp.x != float(NRSG_Capture) || stamp.y != float(NRSG_Generation) ||
        stamp.z != float(NRSG_Frame) || stamp.w != 1.0) return originalNeural;
    if (NRSG_View == 1) return float4(NRSG_Encode(NRSG_ToLinear(source.rgb)), originalNeural.a);

    float3 neural = tex2D(NRSG_Neural, uv).rgb;
    float3 sourceSum = 0.0, neuralSum = 0.0;
    float weightSum = 0.0;
    const float2 pixel = float2(1.0 / BUFFER_WIDTH, 1.0 / BUFFER_HEIGHT);
    // Exact radius-4 joint bilateral B++ reference, not separable approximation.
    [loop] for (int dy = -4; dy <= 4; ++dy) {
        [loop] for (int dx = -4; dx <= 4; ++dx) {
            float2 at = uv + float2(dx, dy) * pixel;
            float3 sampleSource = tex2Dlod(NRSG_Source, float4(at, 0, 0)).rgb;
            float3 delta = sampleSource - source.rgb;
            float distance = delta.x*delta.x / .01 + dot(delta.yz, delta.yz) / .003025;
            distance += float(dx*dx + dy*dy) / 6.76; // (4 * .65)^2
            float weight = exp(-.5 * distance);
            sourceSum += sampleSource * weight;
            neuralSum += tex2Dlod(NRSG_Neural, float4(at, 0, 0)).rgb * weight;
            weightSum += weight;
        }
    }
    float3 sourceBase = sourceSum / weightSum;
    float3 neuralBase = neuralSum / weightSum;
    float strength = saturate(NRSG_Strength);
    float3 correction = sourceBase - neuralBase;
    correction.x *= saturate(NRSG_Tone);
    float3 outputLab = neural + strength * correction;
    outputLab += strength * (clamp(NRSG_Detail, 0.0, 2.0) - 1.0) * (neural - neuralBase);
    return float4(NRSG_Compress(outputLab), originalNeural.a);
}

technique NeuRotic_SourceGuided_Save < ui_label = "Source colours: BEFORE Feeder"; > {
    pass SaveOriginalColourAreas {
        VertexShader = NRSG_VS; PixelShader = NRSG_SavePS;
        RenderTarget = NeuRotic_SourceGuided_SourceLab;
        SRGBWriteEnable = false;
    }
    pass SaveCaptureStamp {
        VertexShader = NRSG_VS; PixelShader = NRSG_StampPS;
        RenderTarget = NRSG_SourceStamp;
        SRGBWriteEnable = false;
    }
}
technique NeuRotic_SourceGuided_Apply < ui_label = "Source-guided colours: AFTER Feeder"; > {
    pass ReadNeuralColourAreas {
        VertexShader = NRSG_VS; PixelShader = NRSG_NeuralPS;
        RenderTarget = NeuRotic_SourceGuided_NeuralLab;
        SRGBWriteEnable = false;
    }
    pass RefineColourAreasAndDetails {
        VertexShader = NRSG_VS; PixelShader = NRSG_ApplyPS;
        SRGBWriteEnable = false;
    }
}
#endif
