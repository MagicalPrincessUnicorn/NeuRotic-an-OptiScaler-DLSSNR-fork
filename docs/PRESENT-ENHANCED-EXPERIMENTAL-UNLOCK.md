# Guided Present combinations

Guided Present processing validates captured guides against the current image, device, dimensions, subrects and submission ordering. Require Guides refuses frames without qualified guides. Auto Guides can use the image-only baseline when guides are unavailable. Invalid guide binding, recording or synchronization does not trigger an unsafe retry.

Frame generation, Ray Reconstruction and Multipass may change guide and history requirements. Requested options are distinct from successful processing; inspect actual activity, pass counts and fallback diagnostics. Scene guides do not describe HUD pixels.

Changing active feature combinations invalidates affected history. Existing GPU completion tracking and resource ownership remain required. See the current release notes for supported workflows; this technical contract does not establish compatibility for every combination.
