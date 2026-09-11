# NeuRotic Alpha 0.9.5 — milestone manifest

## Identity

- Public name: **NeuRotic Alpha 0.9.5**
- Release-note bundle: `NR-BUNDLE-ALPHA-095-A57E4456-R01`
- Public branch/tag: `alpha-0.9.5`
- Implementation branch: `exp/present-enhanced-experimental-unlock`
- Implementation commit: `a57e445604e3aa42c32d92908074ef0b565f04b6`
- Exact parent: `41274da7b86dd1cc1fd88908d8b149ee7fb06495`
- Selected installer ancestor: `e7d46b899740ed25c78f11ae484366a2ba049b75`
- Captured-guide source: `885901ecd2ca3b2c99b813211c297a963971b8bd`
- Parent DLSS-NR comparison tip: `973761621353b99bee3dc7d4bb27b117fef2644f`

The user explicitly selected the completed `a57e4456` build for public publication as
Alpha 0.9.5. This is an Alpha milestone and public experimental release, not a
promotion or repointing of the frozen `nr/stable-postsr` control.

## Included behavior

Alpha 0.9.5 includes the complete ancestry of `a57e4456`: three NR routes,
Present Enhanced captured Native guides and strict original-image fallback, independent
Present NR-resolution policies, Follow Native and custom workloads, visible dimensions,
ten-pass Multipass and shared child tuning, GPU lifetime and configuration hardening,
Vulkan NR repairs, the reorganized localized UI, and the verified customer installer
with explicit proxy/ReShade handling and exact restoration.

Frame Generation, Ray Reconstruction, NR Multipass, and DX11 Present Enhanced attempts
are exposed experimentally. Their removal from blanket compatibility guards is not a
claim that every combination works. D3D12 SDR with those additional features off remains
the comparison control. Vulkan Present routes and HDR Present Enhanced are not implemented.

## Public package

- Asset: `NeuRotic-Alpha-0.9.5.zip`
- Bytes: `131572998`
- SHA256: `BD861DF8E143501D7F31545537E7D1473334F24A3D298BCF7D250967711FA3A9`
- Package manifest SHA256: `44193D5FC1E5EB29D1B215D6F0CD76C331C90787A1D64867608180445AF08553`
- `OptiScaler.dll` SHA256: `33166DB3B17612D791F54C81A471656B5B2302952695B7AF36F97DAE8890A5F6`
- `nvngx.dll_dlssnr.dll` SHA256: `A91892C3A645B12B045F21A542C36CE8534221C1399C6D9396A1BC9BA75FD7F2`
- Packaged `OptiScaler.ini` SHA256: `6C1238B0801044E753B8CB8664B87B73E25C049572F1105B6832531BB7FCCA79`

The proprietary NVIDIA `nvngx_dlssnr.dll` model is not included. The ZIP was created
from the verified a57 handoff with the complete top-level installer folder retained.
A fresh extraction reproduced all 25 manifest entries byte-for-byte, contained 26 files
including the manifest itself, and contained no private model or development outputs.

## Evidence

The implementation checkpoint retains passing source-contract, GPU-safety, guide-copy
and matching, conversion, resolution, forwarder, configuration, robustness, UI,
localization, Release/x64, incremental-build, package, install, restore, conflict,
encoding, tamper, and rollback evidence documented in
`NR-PRESENT-ENHANCED-UNLOCK-2026-09-11.md`.

The public package additionally passed the full customer installer/restore fixture suite
after its Alpha documentation and manifest were finalized. Evidence:
`logs/alpha-0.9.5-public-release/installer-fixtures-389a026c9c444b74bfb5e7d21d1078e1`.

## Runtime status and acceptance

- Result for newly unlocked combinations: **Inconclusive**
- Decision: **publish as Alpha 0.9.5 and keep the affected behavior experimental**
- No live game deployment was performed during publication.
- Sustained Enhanced capture/match/evaluation counters and visual A/B/A testing at
  full output, Follow Native, 67%, and 50% remain the meaningful acceptance evidence.
- FG, RR, Multipass, and DX11 should be tested individually before combined stacks.
- A dropdown selection, lack of a crash, or original-image fallback does not prove
  successful Present Enhanced evaluation.
