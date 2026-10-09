# Standalone worker build

This directory preserves the worker's provider, shader, and vendor interface versions. Its source layout keeps those dependencies local to the worker.

Use a Visual Studio Developer Command Prompt with MSBuild, the v145 toolset (MSVC 14.44.35207), and Windows SDK 10.0.26100.0. From the repository root, run `tools\window-worker\Build-Worker.cmd`. The shader compiler is `OptiScaler/shaders/shader_tools/dxc.exe`.

The project builds the forwarder first, generates its SHA-256 identity header, and then builds the worker. The outputs are `builds/window-worker/bin/NeuRotic.WindowWorker.exe` and `builds/window-worker/bin/nvngx.dll_dlssnr.dll`. Distribute both files together; the worker checks the forwarder's compiled identity.

Builds produce a new matched pair. Compiler paths, toolchain versions, and debug information can change binary hashes, so a source build does not promise the release asset's exact bytes.
