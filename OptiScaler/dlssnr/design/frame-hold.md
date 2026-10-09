# Frame hold

Frame hold captures the scene input to NR so downstream NeuRotic settings can be compared against the same frame. The model and composition can be re-evaluated when these settings change. The held input and its rendering exposure state remain stable until released or invalidated.

The game and its HUD can continue updating. Upstream upscaler settings are not re-evaluated against the held output, and a held frame cannot demonstrate motion behavior. Resource, dimensions, route or device changes must invalidate incompatible held state. GPU completion and resource ownership remain required when replacing or releasing the held image.
