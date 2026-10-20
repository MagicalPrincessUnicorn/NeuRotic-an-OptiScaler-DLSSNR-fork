"""Package the accepted W03 binaries for a reversible, manual game smoke test.

The controlled W03 qualification belongs to its existing host. This installer
uses the accepted product binary; a game run is a separate observation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
ROOT = REPO.parent
SOURCE = ROOT / "_exports/NeuRotic-W03-Closure-20260928-r27.zip"
DESTINATION = ROOT / "_exports/NeuRotic-W03-GameTest-20260928-r27.zip"
ACCEPTANCE = REPO / "docs/ai/live/W03_CONNECTED_ACCEPTED_2026-09-28.json"
PIN = "91b364c4c22829b51a75346114f887da04557f5b36317f75e24b4b2199e62c82"
NAME = "NeuRotic-W03-GameTest-20260928-r27"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def git(*args: str) -> str:
    return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forwarder", type=Path, help="locally built direct NR forwarder")
    parser.add_argument("--output", type=Path, help="fresh output ZIP for the revised forwarder")
    args = parser.parse_args()
    destination = args.output.resolve() if args.output else DESTINATION
    package_name = destination.stem if args.output else NAME
    assert (args.forwarder is None) == (args.output is None), "Forwarder and output must be supplied together"
    assert not destination.exists(), "Sealed output exists; choose a new version"
    assert digest(SOURCE.read_bytes()) == PIN, "Accepted r27 package changed"
    commit = git("rev-parse", "HEAD")
    branch = git("branch", "--show-current")
    assert re.fullmatch(r"[0-9a-f]{40}", commit) and branch == "codex/w03-completion"
    accepted = json.loads(ACCEPTANCE.read_text(encoding="utf-8-sig"))
    assert accepted["status"] == "CONTROLLED_SOURCE_BOUND_TERMINAL_PHYSICALLY_VERIFIED"
    assert accepted["package_sha256"] == PIN
    assert accepted["validation"]["strict_qualification"] == "NOT_GRANTED"
    with zipfile.ZipFile(SOURCE) as prior:
        rows = json.loads(prior.read("PACKAGE_HASHES.json"))
        assert len(rows) == len({r["file"].casefold() for r in rows})
        for row in rows:
            assert digest(prior.read(row["file"])) == row["sha256"], row["file"]
        old_build = json.loads(prior.read("BUILD_TEST_EVIDENCE.json"))
        assert old_build["product_build"]["status"] == "PASS"
        assert old_build["sdk_build_and_test"]["status"] == "PASS"
        assert old_build["provider_game_execution_by_builder"] == "NOT_RUN"
        pins = old_build["binary_pins"]
        assert pins["candidate/OptiScaler.dll"] == accepted["validation"]["product_sha256"]
        assert pins["candidate/amd_fidelityfx_dx12.dll"] == accepted["validation"]["sdk_sha256"]
        compiled = json.loads(prior.read("evidence/r27/product/sources-after.json"))
        revised_forwarder_sources = {
            "OptiScaler/dlssnr/CanonicalProviderIdentity.h",
            "OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp",
        } if args.forwarder else set()
        for name, expected in compiled.items():
            if name in revised_forwarder_sources:
                continue
            assert digest((REPO / name).read_bytes()) == expected, name
        files = {
            "payload\\OptiScaler.dll": prior.read("candidate/OptiScaler.dll"),
            "payload\\nvngx.dll_dlssnr.dll": (args.forwarder.read_bytes() if args.forwarder else
                                                   prior.read("candidate/nvngx.dll_dlssnr.dll")),
            "payload\\OptiScaler\\amd_fidelityfx_dx12.dll": prior.read("candidate/amd_fidelityfx_dx12.dll"),
        }

    ini = (REPO / "integration/OptiScaler.ini").read_bytes()
    text = ini.decode("utf-8-sig")
    for required in ("NativeProtocol=false", "ExperimentalMode = false", "OverrideFgGuardrails = false",
                     "[FrameGen]", "FGInput = auto", "FGOutput = auto", "Enabled = auto"):
        assert required in text, required
    assert not re.search(r"(?m)^\s*Enabled\s*=\s*true\s*$", text)
    files["payload\\OptiScaler.ini"] = ini
    customer = REPO / "tools/packaging/customer"
    files.update({
        "payload\\NeuRotic-LICENSE.txt": (REPO / "LICENSE").read_bytes(),
        "payload\\Licenses\\FidelityFX_v1_LICENSE.md": (REPO / "Licenses/FidelityFX_v1_LICENSE.md").read_bytes(),
        "payload\\Licenses\\DirectX_LICENSE.txt": (REPO / "external/directx_agility_sdk/LICENSE.txt").read_bytes(),
        "payload\\Licenses\\FidelityFX_v2_LICENSE.md": (REPO / "external/FidelityFX-SDK-v2/docs/license.md").read_bytes(),
        "payload\\Licenses\\XeSS_LICENSE.txt": (REPO / "external/xess/LICENSE.txt").read_bytes(),
        "payload\\Licenses\\RenoDX_ATTRIBUTION.txt": (REPO / "Licenses/RenoDX_ATTRIBUTION.txt").read_bytes(),
        "NeuRotic-Manual-Setup.cmd": (customer / "NeuRotic-Manual-Setup.cmd").read_bytes(),
        "NeuRotic-Manual-Uninstall.cmd": (customer / "NeuRotic-Manual-Uninstall.cmd").read_bytes(),
        "Collect-NR-Review.cmd": (customer / "Collect-NR-Review.cmd").read_bytes(),
        "support\\Collect-NR-Review.ps1": (customer / "support/Collect-NR-Review.ps1").read_bytes(),
        "support\\NeuRotic-Setup-Engine.ps1": (customer / "support/NeuRotic-Setup-Engine.ps1").read_bytes(),
        "support\\NeuRotic-Prerequisites.ps1": (customer / "support/NeuRotic-Prerequisites.ps1").read_bytes(),
        "support\\NeuRotic-RuntimeFiles.ps1": (customer / "support/NeuRotic-RuntimeFiles.ps1").read_bytes(),
        "support\\TESTING.txt": (customer / "support/TESTING.txt").read_bytes(),
    })
    build = {
        "status": "built", "commit": commit, "branch": branch,
        "build": {"exit_code": 0, "source_unchanged": True,
                  "evidence": "Accepted r27 supervised product and SDK receipts in source package"},
        "source_package_sha256": PIN,
        "product_sha256": pins["candidate/OptiScaler.dll"],
        "sdk_sha256": pins["candidate/amd_fidelityfx_dx12.dll"],
        "forwarder_sha256": digest(files["payload\\nvngx.dll_dlssnr.dll"]),
        "forwarder_scope": ("direct NR accepts the selected provider by path and required exports; reviewed W03 contract remains pinned"
                            if args.forwarder else "accepted controlled W03 forwarder"),
        "runtime_scope": "Selected controlled source-bound connection physically verified",
        "game_scope": "Experimental manual smoke test pending",
        "provider_model_bundled": False,
    }
    files["support\\BUILD-MANIFEST.json"] = encoded(build)
    files["support\\W03-ACCEPTANCE.json"] = encoded(accepted)
    files["READ ME.txt"] = (
        f"{package_name} - EXPERIMENTAL GAME TEST\r\n\r\n"
        "This package contains the accepted r27 OptiScaler product, a direct NR forwarder, and\r\n"
        "FidelityFX SDK. The accepted controlled-host result is not a game result.\r\n"
        "No game or provider starts during packaging or setup. NR and FG defaults remain off.\r\n\r\n"
        "INSTALL\r\n"
        "1. Extract the complete folder outside the game folder. Close the game.\r\n"
        "2. Run NeuRotic-Manual-Setup.cmd. Select the actual game EXE and a proxy name it loads.\r\n"
        "   For a DirectX 12 game, dxgi.dll is the usual first choice. Read any occupied\r\n"
        "   file prompt before confirming. Setup records backups and installs locally.\r\n"
        "3. The private nvngx_dlssnr.dll model is not bundled. Supply your own beside\r\n"
        "   the game EXE before enabling Neural Rendering. Never copy it to System32.\r\n"
        "4. Start the game yourself. First confirm the game reaches its menu and the\r\n"
        "   proxy loaded. Enable one NR route manually for a short repeatable scene.\r\n"
        "   Record game/API, GPU/driver, route, exact settings and visual result.\r\n"
        "5. The pinned SDK is installed at OptiScaler\\amd_fidelityfx_dx12.dll in the\r\n"
        "   game folder. FSR3 FG remains off until you select it. To test this exact\r\n"
        "   SDK, set [Libraries] FfxDx12Path in the installed OptiScaler.ini to its\r\n"
        "   full installed path, then select FSRFG manually. Confirm the loaded SDK\r\n"
        "   path/hash in the log. A game-supplied SDK may otherwise be selected.\r\n"
        "6. If a failure, device removal or replay uncertainty occurs, stop that\r\n"
        "   session; keep the log and crash data. Do not retry automatically.\r\n\r\n"
        "ADMISSION CAPTURE (r32 forwarder)\r\n"
        "After one manual NR attempt, close the game and run Collect-NR-Review.cmd.\r\n"
        "Paste the game EXE folder. Return the ZIP printed by the collector.\r\n"
        "It collects the log, settings, admission JSON and DLL hashes, never DLLs.\r\n"
        "NeuRotic-NR-admission.json records the exact load/refusal/Init/Create stage.\r\n"
        "The collector also checks this game's diagnostic fallback in Windows TEMP.\r\n"
        "No collector action loads a provider or launches a game.\r\n\r\n"
        "RESTORE\r\n"
        "Close the game and run NeuRotic-Manual-Uninstall.cmd or the installed\r\n"
        "Uninstall NeuRotic.cmd. The managed restore checks ownership and backups.\r\n\r\n"
        "SCOPE\r\n"
        "The r27 controlled source-bound W03 path was physically verified.\r\n"
        "This revised forwarder is locally tested; its game behavior is unverified.\r\n"
        "Direct NR accepts the selected provider by path and required exports.\r\n"
        "The exact W03 lifetime contract still requires its reviewed provider build.\r\n"
        "Canonical original-game identity, Strict/FinalSealId, game compatibility,\r\n"
        "image quality, indefinite lifetime and product promotion are unclaimed.\r\n"
        "See support\\W03-ACCEPTANCE.json and support\\BUILD-MANIFEST.json.\r\n"
    ).encode("utf-8")
    assert not any("nvngx_dlssnr.dll" in n or n.lower().endswith((".pdb", ".exp", ".lib")) for n in files)
    manifest = {
        "kind": "neurotic-customer-candidate", "lifecycle": "experimental-review",
        "name": package_name, "commit": commit, "branch": branch,
        "public_release": False, "deployed": False,
        "runtime_result": "Controlled W03 accepted; game test pending",
        "decision": "Manual experimental game installation only; no product promotion",
        "source_package_sha256": PIN,
        "files": [{"path": n, "bytes": len(raw), "sha256": digest(raw)} for n, raw in sorted(files.items())],
    }
    files["support\\PACKAGE-MANIFEST.json"] = encoded(manifest)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for entry_name, raw in sorted(files.items()):
            assert ".." not in entry_name.split("\\") and ":" not in entry_name
            info = zipfile.ZipInfo(f"{package_name}/{entry_name.replace(chr(92), '/')}")
            info.date_time = (2026, 9, 28, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(destination) as archive:
        assert len(archive.namelist()) == len(files)
        for entry_name, raw in files.items():
            assert archive.read(f"{package_name}/{entry_name.replace(chr(92), '/')}") == raw
    print(json.dumps({"package": str(destination), "sha256": digest(destination.read_bytes()),
                      "bytes": destination.stat().st_size, "files": len(files),
                      "manifest_sha256": digest(files["support\\PACKAGE-MANIFEST.json"]),
                      "commit": commit, "source_package_sha256": PIN}, indent=2))


if __name__ == "__main__":
    main()
