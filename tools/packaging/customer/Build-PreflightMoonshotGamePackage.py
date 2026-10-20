"""Seal the current Preflight/Moonlight/Moonshot build with the proven game installer.

This repacks a verified experimental game installer in memory. It replaces only the
product and forwarder binaries, current build metadata, readme, and package manifest.
It never installs to a game or loads a provider.
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
BASE = ROOT / "_exports/NeuRotic-W03-GameTest-20260928-r32-provider-scope.zip"
BASE_SHA256 = "64ec5518625dfc71161413a8dd1a785cdd1085d7f3d6321129964afd64670002"
PRODUCT = REPO / "x64/Release/OptiScaler.dll"
PRODUCT_SHA256 = "3055eecda79f8128fa54ef8cb9d75361f9637ba3d74a894040b2ad872e61058e"
FORWARDER = REPO / "x64/Release/a/nvngx.dll_dlssnr.dll"
FORWARDER_SHA256 = "1f3c1974e9c708319d7a8e1c3e968fd9d56f0a2fa7b976dc4a20a11a4f402416"
SOURCE_BUILD_COMMIT = "eb537ea1"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def git(*args: str) -> str:
    return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new ZIP path under the export directory")
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.parent == (ROOT / "_exports").resolve(), "Use the workspace export directory"
    assert not output.exists(), "Existing delivery is preserved"
    name = output.stem
    assert re.fullmatch(r"NeuRotic-Preflight-Moonlight-Moonshot-[A-Za-z0-9-]+", name)
    commit = git("rev-parse", "HEAD")
    assert git("branch", "--show-current") == "codex/w03-completion"
    assert not git("diff", "--name-only", SOURCE_BUILD_COMMIT + "..HEAD", "--", "OptiScaler"), "Product source changed since verified build"
    product, forwarder = PRODUCT.read_bytes(), FORWARDER.read_bytes()
    assert sha(product) == PRODUCT_SHA256, "Product differs from verified Release build"
    assert sha(forwarder) == FORWARDER_SHA256, "Forwarder differs from pinned Release build"
    ini = (REPO / "integration/OptiScaler.ini").read_bytes()
    text = ini.decode("utf-8-sig")
    for required in ("NativeProtocol=false", "ExperimentalMode = false",
                     "OverrideMultipassGuardrails = false", "OverrideHdrGuardrails = false",
                     "OverrideFgGuardrails = false"):
        assert required in text, required
    assert not re.search(r"(?m)^\s*Enabled\s*=\s*true\s*$", text)
    assert sha(BASE.read_bytes()) == BASE_SHA256, "Installer source archive changed"

    with zipfile.ZipFile(BASE) as prior:
        prefix = prior.namelist()[0].split("/")[0] + "/"
        prior_manifest = json.loads(prior.read(prefix + "support/PACKAGE-MANIFEST.json"))
        files: dict[str, bytes] = {}
        for row in prior_manifest["files"]:
            path = row["path"]
            assert path not in files
            raw = prior.read(prefix + path.replace("\\", "/"))
            assert len(raw) == row["bytes"] and sha(raw).lower() == row["sha256"].lower(), path
            files[path] = raw
        old_build = json.loads(files["support\\BUILD-MANIFEST.json"])
        assert old_build["provider_model_bundled"] is False
        assert sha(files["payload\\OptiScaler\\amd_fidelityfx_dx12.dll"]) == old_build["sdk_sha256"]
        assert files["payload\\OptiScaler.ini"] == ini

    files["payload\\OptiScaler.dll"] = product
    files["payload\\nvngx.dll_dlssnr.dll"] = forwarder
    files["support\\BUILD-MANIFEST.json"] = encoded({
        "status": "built", "commit": commit, "branch": "codex/w03-completion",
        "build": {"exit_code": 0, "source_unchanged": True,
                  "product_evidence": "support\\UI-REVISION-VERIFICATION.md",
                  "product_source_commit": SOURCE_BUILD_COMMIT,
                  "forwarder_evidence": "pinned VS18 v145 Release build; SHA-256 checked"},
        "source_package_sha256": BASE_SHA256,
        "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
        "sdk_sha256": old_build["sdk_sha256"],
        "runtime_scope": "Accepted prior W03 observations remain bound to their original binaries",
        "game_scope": "This exact UI/identity/opt-in build awaits a manual game smoke test",
        "provider_model_bundled": False,
    })
    files["support\\IMPLEMENTATION-VERIFICATION.md"] = (
        REPO / "docs/implementation-evidence/moonshot-w03-reconciliation/VERIFICATION.md"
    ).read_bytes()
    files["support\\UI-REVISION-VERIFICATION.md"] = (
        REPO / "docs/implementation-evidence/preflight-moonlight/UI-REVISION-VERIFICATION.md"
    ).read_bytes()
    files["support\\UI-REVIEW.md"] = (
        REPO / "docs/implementation-evidence/preflight-moonlight/UI-REVIEW-20260928.md"
    ).read_bytes()
    files["READ ME.txt"] = (
        f"{name} - EXPERIMENTAL GAME TEST\r\n\r\n"
        "This is the current Preflight + Moonlight + Moonshot build. Preflight reads\r\n"
        "live owner observations in a two-column layout, with Not observed where\r\n"
        "data is unavailable. Gray is the fresh default accent; other presets\r\n"
        "remain available. Four experimental choices are independent and off by\r\n"
        "default. Newly enabled choices get one confirmation when Save is pressed.\r\n"
        "The earlier W03 game result belongs to its exact prior binaries;\r\n"
        "this build needs a separate game observation.\r\n\r\n"
        "INSTALL\r\n"
        "1. Extract the complete ZIP outside the game folder and close the game.\r\n"
        "2. Run NeuRotic-Manual-Setup.cmd. Select the game's EXE and a proxy name it loads.\r\n"
        "   For DirectX 12, dxgi.dll is a usual first choice. Read any occupied-file\r\n"
        "   prompt before proceeding. Setup backs up and verifies managed files.\r\n"
        "3. The private nvngx_dlssnr.dll model is not included. Preserve or supply\r\n"
        "   your own beside the game EXE before enabling NR. The included\r\n"
        "   nvngx.dll_dlssnr.dll is the forwarder, not the private model.\r\n"
        "4. Start the game yourself. Check that the proxy loads, then open Neural\r\n"
        "   Rendering > Preflight. Check its observations and missing-data labels.\r\n"
        "   Test one NR route in a short repeatable scene. Record the game/API,\r\n"
        "   GPU/driver, route, exact settings, visual result, and log.\r\n"
        "5. The included FidelityFX SDK remains available under OptiScaler\\. FG\r\n"
        "   and experimental overrides remain off until you select them manually.\r\n"
        "   If there is a device removal or uncertain GPU failure, end the session.\r\n\r\n"
        "RESTORE\r\n"
        "Close the game. Run NeuRotic-Manual-Uninstall.cmd or the installed\r\n"
        "Uninstall NeuRotic.cmd. Managed backups restore original files.\r\n\r\n"
        "EVIDENCE\r\n"
        "After the manual test, run Collect-NR-Review.cmd and return its ZIP.\r\n"
        "The package verifies its files before installation. See support\\\r\n"
        "BUILD-MANIFEST.json, PACKAGE-MANIFEST.json and\r\n"
        "UI-REVISION-VERIFICATION.md for the exact binary and test scope.\r\n"
        "No game or proprietary provider was launched to create this package.\r\n"
    ).encode("utf-8")
    assert not any(path.lower().endswith((".pdb", ".exp", ".lib", ".obj")) or
                   path.lower().endswith("/nvngx_dlssnr.dll") or
                   path.lower().endswith("\\nvngx_dlssnr.dll") for path in files)
    manifest = {
        "kind": "neurotic-customer-candidate", "lifecycle": "experimental-review",
        "name": name, "commit": commit, "branch": "codex/w03-completion",
        "public_release": False, "deployed": False,
        "runtime_result": "Prior W03 result retained; exact new game test pending",
        "decision": "Manual experimental installation only; no product promotion",
        "source_package_sha256": BASE_SHA256,
        "files": [{"path": path, "bytes": len(raw), "sha256": sha(raw)}
                  for path, raw in sorted(files.items())],
    }
    files["support\\PACKAGE-MANIFEST.json"] = encoded(manifest)
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            assert ".." not in path.split("\\") and ":" not in path
            member = f"{name}/{path.replace(chr(92), '/')}"
            info = zipfile.ZipInfo(member)
            info.date_time = (2026, 9, 28, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert len(archive.namelist()) == len(files)
        for path, raw in files.items():
            assert archive.read(f"{name}/{path.replace(chr(92), '/')}") == raw, path
    print(json.dumps({"zip": str(output), "sha256": sha(output.read_bytes()),
                      "bytes": output.stat().st_size, "members": len(files),
                      "manifest_sha256": sha(files["support\\PACKAGE-MANIFEST.json"]),
                      "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
                      "sdk_sha256": old_build["sdk_sha256"], "commit": commit}, indent=2))


if __name__ == "__main__":
    main()
