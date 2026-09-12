# Cyberpunk combined RR + FG corrected candidate

This source supersedes the original combined candidate `eef3e66d29839c8cc7909eaace97e10c5648946a`.
The original remains preserved under `archive/candidate-cyberpunk-combined-rr-fg-eef3e66d`.
The moving annotated tag `candidate/cyberpunk-combined-rr-fg` identifies this corrected source.

## Accepted behavior

Cyberpunk 2077 ran Present Image Only Neural Rendering with native Ray Reconstruction,
native DLSS Frame Generation 2x, Follow Output Resolution and Multipass off. Present NR
publishes the exact backbuffer copyback completion to native FG, whose command-list submission
waits for that completion. Missing identity, lifecycle changes and failed dependencies remain
fail-closed without a CPU wait.

The maintainer audit additionally corrects completion-record lifetime, duplicate claims,
provider/native generation races, failed GPU-wait forwarding, fence ownership, failed native
feature releases and shared NGX observation races. NR Off and the Native route invalidate old
completion handoffs before forwarding the next ordinary Present while retaining unfinished GPU
work until its fence completes.

## Evidence

The original accepted combined session `3eb5efbf3eb849fc964cd1857eef0c6b` produced 518 NR
outputs and 208 exact copyback/wait/FG chains. The first audit deployment exposed an NR-Off
regression in session `9755fff3976e461d8a626af8abd38d71`: 237 native-FG evaluation failures
began immediately after two NR-Off transitions, producing a looping frozen image. Cycling the
game's FG state cleared the stale completion record.

The correction was installed with proxy SHA256
`4FD915438FA5F1BFEB421628CD25CCDE5A6592E07E9466091D1CBA59679B2B2D` and forwarder SHA256
`65F8EB4E4FA687D9B19864764683A790DD04E6DC6A01D083C9328719E4EBA3BD`. The live INI remained
`CD3D9E9908A3A61B4CDADFE2689E9444511DDADEB885CE12988C3D944A8D044D`; the patched NR model
remained `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`.

Corrected session `d513ad8d15714c1080714803ab389ead` exited 0 with no DRED fault. After NR Off,
the complete retained log records 167 native-FG evaluations and 668 successful Presents with
zero native-FG evaluation failures. The user reported the transition worked. Result: Better.
Failed pre-readiness session `f5ca6fdf26f14eb29739153b8e296797` is excluded.

Focused tests include 32 delayed-completion NR Off/On cycles, claimed and skipped FG packets,
repeated disabled Presents, 16 concurrent claims, concurrent native feature registry operations,
handle reuse, failed release, overflow, invalid inputs and failed wait suppression. Pre-FG WARP,
GPU safety, Present guides, lifecycle, robustness, localization and integration checks pass.
The Release/x64 build and exact-source mechanical patch checks pass.

## Scope

This is the viable release-integration candidate for the tested Cyberpunk renderer and NR-Off
transition. It is not a public release or stable baseline. Present Enhanced, Multipass, other
providers and games, long-session acceptance and separate image-quality comparison remain outside
this result. Integration into a different release/UI lineage requires conflict resolution, a new
clean build/install record and focused runtime review.
