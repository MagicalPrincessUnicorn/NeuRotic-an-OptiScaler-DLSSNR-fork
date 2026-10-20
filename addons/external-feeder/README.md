# External Feeder prepared consumer

This optional capture-only addon uses `DLSS5_Feed.fx` adapted from
DLSS5-Feeder **v1.18.0-beta.1**, commit `a9d071c`. The shader and adapted provider
bindings retain Jean-Laurent ROUZIES's MIT notice. NeuRotic changes the D3D9
warning and overlay text to report unavailable capture and the NR Anything fallback;
shader processing is unchanged. The original archive is
preserved in `the upstream source archive` (SHA256
`6491e50d4eba56cd742c52bb996473d7ea140484e0b164b61bf1f01e0a2e9eb5`).

The consumer is a small replacement for the donor addon's NGX evaluation and
helper code. It calls `NeuRotic_EvaluateExternalFeederV1` in the prepared addon
after the donor shader completes. The source descriptor, full-resolution pixel
vectors and distrust plane go through the same API adapters and NeuRotic
Processor as built-in inputs. No manufactured DLAA consumer is started.

## Test installation

1. Close the game. Install the experimental NeuRotic package and a compatible
   ReShade runtime for the game's bitness.
2. Select **External feeder** as NR input source and GPU transport; restart.
   Copy the matching `NeuRotic-ExternalFeeder.addon64` or `.addon32` alongside
   `NeuRotic-PreparedGuides` in ReShade's addon search directory.
3. Put `DLSS5_Feed.fx` in the effect search directory. Supply a supported motion
   provider separately and enable its technique above `DLSS5_Feed`. Set
   `DLSS5_MV_PROVIDER` to match it. Leave `NeuRotic_PreparedGuides.fx` disabled
   for this route. Do not load the stock `dlss5-feed`/RenoDX neural consumer.
4. Set `RESHADE_DEPTH_INPUT_IS_REVERSED` explicitly to the correct `0` or `1`.
   Use motion sign `(1,1)`, scale `1`, and disable fitted geometry. The shader
   converts the selected provider's UV/grid field to full-resolution pixels.
5. Enable NR. Check that input, model completion, and copyback counts advance.
   Copyback is completion evidence; it does not verify what was displayed.

For x86, keep the complete `NeuRotic.GpuHost` directory beside the prepared addon.
The x64 core and private model remain in the managed host. The ordinary x64
proxy cannot be installed into an x86 game. Third-party motion shaders are
separately supplied under their own licenses.

Both addon bitnesses compile locally. D3D transports and helper pixels are tested
with software D3D drivers and an explicit test recorder. ReShade/game/private
model and vendor OpenGL interop still require runtime qualification. Missing
motion/depth, competing consumers, unknown semantics, stale captures and unknown
completion preserve the original image. Direct legacy D3D9/DirectDraw/Glide
capture is not supplied; use NR Anything when no compatible in-game integration
is available. Legacy compatibility is planned as native NeuRotic integration.
