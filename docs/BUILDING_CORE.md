# Building the App and in-game runtime

Use Windows x64 with a Visual Studio C++ installation, Windows SDK, MSBuild, CMake 3.23 or newer, Ninja, DXC and the .NET 10 SDK. Start an x64 Visual Studio developer PowerShell, with CMake, Ninja, DXC and dotnet on PATH. Initialize the pinned dependencies from the repository root:

```powershell
git submodule update --init --recursive
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build/Build-Core.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build/Build-App.ps1
```

The default C++ toolchain selection is v145 with MSVC 14.44.35207. Both scripts accept `-PlatformToolset` and `-VCToolsVersion` for another installed compatible toolchain. Add `-PlanOnly` to display the resolved commands without creating files or running a compiler. `Build-App.ps1 -NativeOnly` builds only the native desktop App.

The runtime recipe first builds the isolated native optical-flow libraries from `addons/prepared-guides/flow` and the pinned FidelityFX SDK. It then builds the Release x64 core with the repository's dependency includes/libraries and freshly generated version headers. It disables the old project pre/post-build packaging events. It never writes into SDK source or copies files into a game. The App recipe builds the native App and publishes its self-contained x64 Discovery helper using the .NET SDK and locked package restore.

Outputs and reusable intermediate directories are under `builds/source/core` and `builds/source/app`. The native runtime DLL is in `builds/source/core/bin`; the App and Discovery helper are in `builds/source/app/bin`. These are developer build outputs, not a complete installer. Preserve all component licenses when assembling a distribution. Models and provider files supplied separately by the user are not part of this source build.

The release download remains the tested installer. A local rebuild has a new version/date label and can differ with the installed SDK, resource compiler or compiler version; this recipe does not promise identical release binary hashes or new game qualification. See the [worker build instructions](../apps/NeuRoticWindowWorker/shipping/BUILDING.md) for the companion worker and forwarder.
