# Neural-rendering forwarder

The neural-rendering forwarder nvngx.dll_dlssnr.dll is distinct from the separately supplied model nvngx_dlssnr.dll. The forwarder provides the calling interface used by NeuRotic; a compatible model is still required.

The optional proxy path uses the driver core's feature creation and evaluation interface. Selecting it does not establish successful initialization, and it is not an automatic replacement for the packaged forwarder. Initialization and feature failures must remain visible through the normal fallback and diagnostic paths.

Preserve matched forwarder interfaces, model compatibility, resource ownership and GPU completion handling when changing this integration.
