# Indiana Jones Vulkan NR resource-parameter experiment

Date: 2026-09-10. Branch: `exp/indiana-jones-vulkan-nr-resource-params`.
Exact parent and failing control: `eafc8203c47c2ebef35d39129fe78ef10ed53223`.
Result: **Inconclusive pending game evidence**. Decision: **keep experimental**.
This is the explicitly approved continuation of an inconclusive experiment, not a promotion.

## Evidence and hypothesis

Tester log: `C:\Users\Josh Parke\Downloads\OptiScaler (IJ Take 3).log`.
SHA256: `44FB02CD32B03D56B4204F08F4528205665E6B01A1830C291EFBB1E6C6678D98`.
Its routing markers confirm selected handle 1000000 / viewport 1 / physical output 3440x1440,
auxiliary handle 1000001 / viewport 2 / 1146x480 bypass, and duplicate suppression.
At 22:04:32.756368, typed creation parameters passed. Creation returned 1 at 22:04:32.912030.
The first evaluation returned `0xBAD00005` (NGX InvalidParameter) at 22:04:32.912237.
No successful NR composition was recorded; neither visual tuning nor FG+NR stability was established.

The parent wrote `DLSSNR.Color`, Depth, MVec and Output through the typed unsigned-long-long setter.
NVIDIA's public Vulkan helpers pass `NVSDK_NGX_Resource_VK` wrappers using void-pointer parameters:
https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_helpers_vk.h
The same behavior is present in the pinned SDK under `external/nvngx_dlss_sdk`.
Correcting this mismatch is the one rendering-variable change. Public DLSS documentation does not
prove the private NR model's complete input contract; runtime success is still required.

## Implementation and boundaries

- Frame preparation uses `Set(key, void*)` and checks `Get(key, void**)` for exact pointer equality.
  It uses the public typed interface, never guessed raw vtable slots or fallback integer writes.
- Input wrapper discriminants are checked before image-union fields. Image/view handles, extents,
  format and nonempty subresource ranges are checked. Model output must be writable. Existing
  zero-base subrects must fit each actual resource, including motion independently from depth.
- Each active model's creation settings are read back without rewriting. Per-frame flags, subrects
  and 1.0 motion scales retain their parent values and receive typed readback checks. Only the
  owned model parameter block is written; the game's RR block and applied tuning are unchanged.
- The shared tested gate prevents vendor evaluation on failed validation/readback. The normal
  session failure latch bypasses further NR and leaves native RR intact. Before this gate, private
  encode work may already have been recorded; no resolve back into the game image occurs on failure.
- First three frames, every 300 successful recorded frames and configuration switches log resource
  keys, pointer setter/readback, wrapper/image/view identity, extents, format and writable state.
  Preparation failure logs the key, reason and readback code/pointers. Model failure logs resource
  details even outside periodic sampling. No per-frame retries or alternative representations.
- Preserve primary routing, auxiliary exclusion, duplicate suppression, the eight retained slots,
  history reset and shutdown-only retirement. No queue, layout, composition, D3D12, FG-policy,
  configuration default, INI or NVIDIA model DLL changes. The existing v2 forwarder ABI is unchanged.

## Offline verification

`tests\Run-VulkanNrFrameParams.cmd` compiles a strict implementation of the real SDK virtual interface.
It rejects the parent's integer resource writes when read as pointers. It tests all four corrected
bindings, missing/wrong-type/mismatched readbacks, null/invalid wrappers, invalid dimensions/subrects,
read-only output, scalar mismatches, reset updates, no model callback on failure, unchanged creation
values/write counts and a separate untouched game parameter block. Source guards check the actual
evaluation gate and absence of explicit evaluation-time waits, release or image destruction.
Rerun `Run-VulkanNrTuning.cmd`, `Run-VulkanRrRouting.cmd` and `Run-NrRobustness.cmd` as controls.
These are CPU/static/regression checks, not proof of model consumption or GPU/presentation behavior.

Review and commit before the canonical build-only command:
`C:\OptiScaler-NR-Dev\scripts\Build-Install.cmd indiana-jones-vulkan-nr-resource-params -BuildOnly`.
Retain exact build manifest, stdout/stderr, source immutability and output hashes.

## Delivery and runtime gate

The tester loads OptiScaler as **winmm.dll**. Package the built OptiScaler DLL under that name,
plus the unchanged matching `nvngx.dll_dlssnr.dll` forwarder. Do not instruct this installation to
replace only an unused `OptiScaler.dll`. Archive hash/contents and DLL hashes must be verified.
Use only the game-local winmm.dll, never Windows/System32. Close the game and back up the existing
two DLLs before replacement; restore those backups to roll back. Preserve the INI and large model.
No local game installation or baseline promotion is authorized.

1. RR on, FG off: enable NR; confirm it processes frames without the red failure message.
2. If successful, compare one structure/intensity change and return to the original configuration.
3. If successful, enable FG and close/reopen the menu.
4. Return one log and a short visual/freeze report. Stop after first failure, without extra toggling.

Acceptance requires successful NR evaluation/composition, visible tuning response, stable primary
routing and FG-on operation. `successfulNR` means successful API return and composition command
recording, not proven GPU completion or presentation. If InvalidParameter persists, use the named
readbacks and metadata for the next focused experiment rather than adding broad resource guesses.
