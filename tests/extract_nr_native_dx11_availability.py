"""Copy the real initialization/availability boundary into the CPU fixture.

The fixture stubs external services; it does not copy the cache algorithm or
claim actual D3D11/D3D12 initialization, lifetime or quarantine qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--lifecycle-source", type=Path, required=True)
args = parser.parse_args()
raw = args.source.read_bytes()
text = raw.decode("utf-8").replace("\r\n", "\n")
anchor = "    if (!runtime.Initialize(context, reason))"
assert text.count(anchor) == 1, "initialization boundary changed"
position = text.index(anchor)
start = text.rfind("    std::string reason;", 0, position)
end = text.index("    auto chain =", position)
assert start >= 0 and end > start
fragment = text[start:end].encode("utf-8")
args.output.write_bytes(fragment)
outputs = {args.output.name: hashlib.sha256(fragment).hexdigest()}
for method in ("Prepare", "Complete"):
    method_start = text.index("void Feature::" + method + "(")
    start = text.index("    Dx11Transport::ContextLock contextLock(context);", method_start)
    preceding = "    auto lifecycleLock = LockBridgeLifecycle();\n"
    if text[:start].endswith(preceding):
        start -= len(preceding)
    end_marker = "    std::lock_guard lock(runtime.mutex);"
    end = text.index(end_marker, start) + len(end_marker)
    name = "availability_" + method.lower() + "_locks_under_test.inc"
    prefix = (text[start:end] + "\n").encode()
    (args.output.parent / name).write_bytes(prefix)
    outputs[name] = hashlib.sha256(prefix).hexdigest()

owner_raw = args.lifecycle_source.read_bytes()
owner = owner_raw.decode("utf-8").replace("\r\n", "\n")
names = ["std::unique_lock<std::recursive_mutex> LockBridgeLifecycle()",
         "std::uint64_t BridgeLifecycleGeneration()",
         "void NotifyDeviceInit(ID3D12Device* device)"]
if "bool BridgeSessionUnavailable()" in owner:
    names.append("bool BridgeSessionUnavailable()")
functions = []
for name in names:
    assert owner.count(name) == 1, name
    start = owner.index(name)
    brace = owner.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (owner[end] == "{") - (owner[end] == "}")
        end += 1
    functions.append(owner[start:end] + "\n")
close = "    if (!g_sessionClosed) ++g_lifecycleGeneration;\n    g_sessionClosed = true;\n"
guard = '    if (g_sessionClosed || g_shutdownFailed) return reject("NR session is closed; restart required");\n'
assert owner.count(close) == 1 and owner.count(guard) == 1
owner_fragments = {"availability_lifecycle_under_test.inc": "\n".join(functions),
                   "availability_close_under_test.inc": close,
                   "availability_session_refusal_under_test.inc": guard}
for name, content in owner_fragments.items():
    data = content.encode()
    (args.output.parent / name).write_bytes(data)
    outputs[name] = hashlib.sha256(data).hexdigest()
print(json.dumps({"source_sha256": hashlib.sha256(raw).hexdigest(),
                  "lifecycle_source_sha256": hashlib.sha256(owner_raw).hexdigest(),
                  "files": outputs,
                  "scope": "actual availability/owner transitions/lock prefixes; external CPU services and retirement stubbed"}))
