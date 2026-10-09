# Building NeuRotic

Use the released installer for normal installation. Source builds produce new binaries and can differ from release files because paths, compiler versions, timestamps, and debug information affect their hashes.

Initialize the repository's pinned submodules before building. Keep their recorded revisions and licenses. Use a Visual Studio Developer Command Prompt with the C++ tools and the Windows SDK versions described in the component instructions.

- [Core runtime and desktop App](docs/BUILDING_CORE.md)
- [Standalone NR Anything worker](apps/NeuRoticWindowWorker/shipping/BUILDING.md)

The standalone worker's source and provider dependencies are kept together in its shipping directory. Its build creates a forwarder first, pins that binary's identity, and compiles the worker against that pin. Package the resulting worker and forwarder together.

Neural-rendering models and any proprietary provider files are supplied separately. Building these components does not download a neural-rendering model or establish game or hardware compatibility.
