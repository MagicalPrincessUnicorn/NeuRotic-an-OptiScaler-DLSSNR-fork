# Present rendering compatibility

Present routes process the presented image where the graphics API, surface format, device and synchronization capabilities are supported. Native Temporal requires compatible game-provided inputs.

DirectX 11, DirectX 12 and Vulkan have separate integration paths. Format conversion and guide availability depend on the active path; selecting a route does not establish that processing is active. Use the rendering diagnostics and a live comparison to check the result.

Require Guides refuses frames without qualified guides. Auto Guides can use an image-only baseline and an admitted or full-output workload when guides are unavailable. Unsupported surfaces and invalid binding, recording or GPU synchronization retain refusal or original-image fallback. Frame generation, HDR and Ray Reconstruction combinations require compatible inputs and providers; support is not universal.

For installation and current release information, see [NeuRotic](README.md).
