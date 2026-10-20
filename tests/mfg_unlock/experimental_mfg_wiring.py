"""Supplementary integration checks; native tests exercise the production core."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
read = lambda p: (ROOT / p).read_text(encoding="utf-8")
# Reviewed Inspector donor includes protected-page reads (41dc42d8) and bounded
# hook stack allocations (6ba1a078). Preserve those Ada repairs exactly while
# the independent RTX20/30 backend and native request guidance are integrated.
base = "11dad02e374fd734a4f052172cdb33a16bb8cd26"
for path in ("OptiScaler/mfg/MfgAdaUnlock.cpp", "OptiScaler/mfg/MfgAdaUnlock.h"):
    original = subprocess.check_output(["git", "-C", str(ROOT), "show", f"{base}:{path}"])
    assert (ROOT / path).read_bytes().replace(b"\r\n", b"\n") == original.replace(b"\r\n", b"\n"), path

config = read("OptiScaler/Config.cpp")
ini = read("OptiScaler.ini")
for family in ("20", "30"):
    key = f"ExperimentalUnlockRTX{family}"
    assert re.search(rf"^{key}\s*=\s*false\s*$", ini, re.M)
    assert f'readBool("DLSSG", "{key}").value_or(false)' in config
    assert f'"{key}"' in config[config.index("bool Config::SaveIni"):]

menu = read("OptiScaler/menu/menu_common.cpp")
controls = read("OptiScaler/menu/ExperimentalMfgControls.h")
assert all(controls.count(f"###ExperimentalMfgRTX{family}") == 1 for family in (40,30,20))
assert "experimental.saveFailed" in menu and "experimental.saved" in menu
assert "PrepareAtBoundary" not in menu
fg_section=menu[menu.index("void MenuCommon::RenderFrameGenerationSelection"):]
fg_section=fg_section[:fg_section.index("\nvoid MenuCommon::",1)]
# The overlay ratio layout and the MFG donor's adjacent 40/30/20 controls must
# coexist in the same FG child page after integration.
assert fg_section.index("if (ctx.childPage == 0)") < fg_section.index('"Override DLSSG Ratio"') < fg_section.index('"Experimental GPU compatibility"') < fg_section.index("ExperimentalMfgControls(")
assert controls.index("###ExperimentalMfgRTX40") < controls.index("###ExperimentalMfgRTX30") < controls.index("###ExperimentalMfgRTX20")
assert "EM::CanEdit" not in fg_section
assert "experimentalLimit" in fg_section and "SelectableCeiling" in fg_section
hook = read("OptiScaler/hooks/Streamline_Hooks.cpp")
assert "RestoreBorrowedOptions(newOptions, options)" in hook
assert "captured.value.numFramesToGenerate, forwardedOptions.numFramesToGenerate" in hook
assert "GetDeviceRemovedReason()" in hook and "Experimental::DeviceRemoved()" in hook
runtime = read("OptiScaler/mfg/ExperimentalMfgRuntime.cpp")
assert "CanReport(r.state.stage,stage)" in runtime
assert "MayPoisonDevice(r.state.family,r.state.backendLoaded)" in runtime
assert "NewProviderNeedsValidation" in runtime
assert "FindQualifiedMfgGates" in runtime and "blackwelltemporal::Commit" in runtime
assert "GET_MODULE_HANDLE_EX_FLAG_PIN" in runtime

base_dir = ROOT / "external/mfg_ampere"
manifest = json.loads((base_dir / "UPSTREAM.json").read_text())
for entry in manifest["files"]:
    assert hashlib.sha256((base_dir / entry["path"]).read_bytes()).hexdigest() == entry["local_sha256"], entry["path"]
assert not any(base_dir.rglob("*.dll"))
assert not any(base_dir.rglob("*.cubin"))
assert not any(base_dir.rglob("*.ptx"))
print("PASS: existing Ada implementation, default-off persistence, shared routes, failure gates and imported source provenance")

assert hook.count("GameFgScope") >= 4
assert "DetourAttach(&(PVOID&) o_slAllocateResources, hkslAllocateResources)" in hook
assert "DetourDetach(&(PVOID&) o_slAllocateResources, hkslAllocateResources)" in hook
ngx=read("OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp")
assert ngx.count("Experimental::AllowFeatureCall()") >= 2
assert runtime.count("createObserver.IsCurrent") >= 3
print("PASS: FG-section adjacency, current ratio ceiling, Create integrity and Streamline-only vendor admission wiring")

assert "requestedOff" not in hook
assert "AllowFeatureCall(forwardedOptions.mode == sl::DLSSGMode::eOff)" in hook
assert "if(!GameFgScope::Current())return false;" in runtime
assert "ArchitectureScope experimentalScope(isFG && boundExperimentalAdapter &&" in ngx
