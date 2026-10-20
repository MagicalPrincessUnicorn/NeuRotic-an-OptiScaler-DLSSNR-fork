# NRH2 GPU helper

The GPU helper is a separate, explicitly selected x64 consumer. CPU v1, its
request/reply layout, and public DLAA mode remain unchanged. The production
entry point loads only its managed sibling `OptiScaler.dll` and invokes the
existing prepared ABI through the single `NativeGuides::Processor` owner.

`PreparedGpuClient` launches one child through a private inherited bootstrap
handle and a local, current-user-only named pipe. Both ends verify the actual
pipe PID and pinned process creation time. A fresh random nonce binds every
registration and ticket. No renderer path is accepted in a frame message.
`PreparedGpuProtocol.h` serializes every descriptor field explicitly; it never
casts received bytes into native structures. NRH2 outer messages remain bounded
to 4096 bytes, with the separate NRG2 ticket as a 120-byte subrecord.

Register IDs 1 and 2 are the host-created producer/output fences. Texture IDs
start at 3 and strictly increase. At most 14 textures and two shared fences are
accepted; current consumers use ten textures in two slots. Texture roles and
formats are fixed: RGBA8 color/output, R32F depth, RG16F motion, R8 distrust.
All imported texture aliases meet at COMMON. Private canonical inputs use the
host's exact device and NON_PIXEL_SHADER_RESOURCE; private output returns UAV.

There are two directional Register exchanges. ProducerToHost carries a handle
already duplicated into the pinned host and receives an identical acknowledgement.
HostToProducer uses `handle=0` solely as an allocation request, with the exact
shape/role/access fields. The host replies with the newly duplicated handle in
the pinned producer's handle table. Zero is never imported or bound as an object.
The client verifies the response and actual local import before retaining it.
`Resource(id)` supplies the borrowed local D3D12 alias for an API-specific NT
handle export/import. This allocation request convention completes the packet's
otherwise unspecified host-creation negotiation without changing message sizes.

Only one frame may be submitted or output-leased. Host canonical slots remain
immutable while Processor history retains their leases. Resources, module,
command recording objects and peer handles remain retained on uncertain work.
A process-wide client gate prevents creating additional helper generations after
quarantine. There is no timeout-based GPU completion, CPU fence signal, implicit
previous-frame result, or automatic fallback after reference. Resize requires
known completed teardown and a fresh owner/nonce/registration generation.

`GpuRemoteConsumer` adapts the existing completed local `TextureCapture` into
NRH2, copying to the two shared ingress slots and back into private local UAV
outputs. Its caller must finish exact game copyback and API state restoration,
then call `ReturnAfterCopyback` with that owner's actual completion proof.
The resulting local queue fence is observed before Release is sent. The output
lease must expire before another Process call. x86 builds never load a model.

Build: `addons/prepared-guides/helper/build_gpu_helper.cmd` reuses the pinned
flow libraries in `builds/universal-guides/flow` and emits the production helper
under `builds/universal-connections/helper/production`. The existing CPU build
script calls this after its unchanged CPU build. The managed installation must
also supply the matching x64 core, flow dependencies and private model setup.

Checks: `tests/universal_connections/build_helper_gpu.cmd`, followed by
`run_helper_gpu_tests.cmd`. The suite builds x86/x64 codecs and runs real x86 and
x64 producers against the x64 host, shared resources in both creation directions,
three-frame reuse, local bridge copyback, actual PID/creation/nonce negatives,
non-GPU objects, multiple sessions, replay, preparing-only, pending-producer and
refused-seal quarantine. The shader recorder reads all four input planes and
verifies exact returned pixels. It does not substitute for private NR.

Observed locally on 2026-10-05: Microsoft Basic Render Driver, LUID
`00000000:00005104`. All listed connected cases passed on software D3D12.
RTX/private-model, hardware cross-API sharing, host-derived-flow image quality,
game behavior, and deployment acceptance remain qualification work. The full
packet's process-kill timing matrix is not exhausted by these tests.
