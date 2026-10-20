# Depth Anything V2 Small developer workflow

The optional NR Anything implementation is source ready. NVIDIA runtime execution, TensorRT parity, real-image quality, throughput and soak qualification remain open. Estimated depth is not yet bound to the NR model. No installer is produced by these tools.

Depth defaults to Off. In NR Anything, choose **Advanced look → Depth Anything V2 Small**, then **Preview depth without NR** for independent preview. Reference is 518×518; Fast is 518×294. Requested 30/60 Hz is a scheduling limit, not measured throughput. The source is SDR BGRA8, at most 3840×2160. Results more than 250 ms old are discarded after GPU retirement. The preview resets its range at large changes detected from 256 display-RGB samples; this heuristic does not claim media seek identity.

The ordinary App and worker do not need CUDA, TensorRT or Python to launch. Selecting independent preview requires a prepared local cache and a matching NVIDIA capture adapter. Selecting depth alongside NR leaves NR image-only while the depth consumer is unqualified. Missing assets affect only the selected optional depth route.

## Build and verify

Run from the existing flagship checkout with Visual Studio 18 / MSVC 14.44 and Windows SDK 10.0.26100. Reuse `builds/depth-anything-v2`, `builds/window-worker` and `builds/hub`.

```bat
tools\depth-anything-v2\Build-Shaders.cmd
tools\depth-anything-v2\Build-Backend.cmd
tools\depth-anything-v2\Build-Prepare.cmd
tools\window-worker\Build-Worker.cmd
tools\hub\Build-Native.cmd
tools\depth-anything-v2\Test-Contracts.cmd
tools\depth-anything-v2\Test-Module.cmd
tools\depth-anything-v2\Test-Gpu.cmd
tools\depth-anything-v2\Test-Preferences.cmd
tools\depth-anything-v2\Test-App.cmd
```

`Build-Backend` / `Build-Prepare` use developer headers under `_deps/depth-anything-v2/cuda-12.9/Library/include` and `tensorrt-10.13/include`. With Python's `zstandard` extraction dependency available, running `Fetch-Headers.py` acquires official headers and checksum-verified CUDA packages into that dedicated directory. These commands compile the optional module and standalone cache builder; they do not install drivers or invoke a game.

## Model export and local engine preparation

Use a dedicated Python environment without xformers. The verified export environment used Torch 2.6.0+cpu, Torchvision 0.21.0+cpu, ONNX 1.17.0 and ONNX Runtime 1.20.1. `bootstrap_dependencies.py` downloads the locked official source archive and Small checkpoint into explicit developer storage, verifies the checkpoint, and records source-file hashes. Preserve upstream notices and Apache-2.0 model/source licensing. Packet-derived helpers retain their MIT notices in `PACKET-LICENSE`; vendor runtimes retain their own licenses.

```bat
python -B tools\depth-anything-v2\bootstrap_dependencies.py --help
python -B tools\depth-anything-v2\export_onnx.py --source _deps\depth-anything-v2\Depth-Anything-V2 --checkpoint _deps\depth-anything-v2\checkpoints\depth_anything_v2_vits.pth --output _deps\depth-anything-v2\exports\reference\model.onnx --width 518 --height 518 --verify-ort
```

The exporter also writes `model.manifest.json`, `parity-input.r32` and `parity-output.r32`. Repeat with height 294 and an independent `exports/fast` output after reference qualification. `--reuse-verified-export` checks the existing pinned export before repeating its synthetic parity test. Trusted ONNX hashes are recorded in `Dav2EngineCache.h` and the evidence manifests; a different exporter result needs explicit review rather than bypassing those pins.

On a matching NVIDIA machine, obtain compatible **official** Windows x64 CUDA 12 / TensorRT 10 runtime files and run the standalone builder outside capture/render callbacks:

```bat
python -B tools\depth-anything-v2\PrepareCache.py --export _deps\depth-anything-v2\exports\reference --runtime C:\absolute\official-runtime --destination builds\window-worker\bin\depth-anything-v2\reference --device 0
```

The supplied runtime directory must contain `cudart64_12.dll`, `nvinfer_10.dll`, `nvonnxparser_10.dll` and `nvinfer_builder_resource_10.dll`. The builder generates the engine locally, checks GPU numerical parity, records GPU UUID/compute capability, complete NVIDIA driver file version, CUDA and TensorRT versions, and hashes all runtime assets. Publication is atomic; an existing cache is preserved. A failed `.building` directory is never promoted automatically. The App uses the worker-adjacent `depth-anything-v2/reference` or `fast` directory; CLI can select another absolute `--depth-root`.

```bat
builds\window-worker\bin\NeuRotic.WindowWorker.exe --run --window HWND --depth-provider dav2 --depth-profile reference --depth-hz 30 --depth-preview --depth-root C:\absolute\verified-cache
```

Reference first: verify real CUDA/D3D interop and partial-failure retirement, diverse images, fresh-result latency/throughput, repeated pause/crop/resize/Stop and long-running stability. Then qualify Fast and 60 Hz. Qualify estimated-depth NR binding separately; do not reinterpret relative depth as native device Z or enable implicit CPU motion.
