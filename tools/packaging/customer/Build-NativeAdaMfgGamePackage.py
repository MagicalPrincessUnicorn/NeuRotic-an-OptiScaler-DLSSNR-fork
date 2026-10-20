"""Seal the verified native Ada MFG build with the existing reversible game installer."""

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
SOURCE_BUILD_COMMIT = "9adfbb45e466942825f400173423e4bb06012560"
PRODUCT = REPO / "x64/Release/a/OptiScaler.dll"
PRODUCT_SHA256 = "37767c8a2503fac1fb44c3148d0926fed047ef43dd1f0b953c69dc0c61482beb"
FORWARDER = REPO / "x64/Release/a/nvngx.dll_dlssnr.dll"
FORWARDER_SHA256 = "e19698aecaa53678794578a2dd28ea4dfd8a303837943e57a41d40b6efa13845"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def git(*args: str) -> str:
    return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.parent == (ROOT / "_exports").resolve(), "Use the workspace export directory"
    assert not output.exists(), "Existing delivery is preserved"
    name = output.stem
    assert re.fullmatch(r"NeuRotic-Native-Ada-MFG-[A-Za-z0-9-]+", name)
    assert git("branch", "--show-current") == "codex/native-mfg-experimental"
    assert not git("diff", "--name-only", SOURCE_BUILD_COMMIT + "..HEAD", "--", "OptiScaler")
    assert not git("diff", "--name-only", "--", "OptiScaler"), "Product source has uncommitted changes"
    product, forwarder = PRODUCT.read_bytes(), FORWARDER.read_bytes()
    assert sha(product) == PRODUCT_SHA256, "Product differs from verified Release build"
    assert sha(forwarder) == FORWARDER_SHA256, "Forwarder differs from verified Release build"
    assert sha(BASE.read_bytes()) == BASE_SHA256, "Installer source archive changed"

    with zipfile.ZipFile(BASE) as prior:
        prefix = prior.namelist()[0].split("/")[0] + "/"
        rows = json.loads(prior.read(prefix + "support/PACKAGE-MANIFEST.json"))["files"]
        files: dict[str, bytes] = {}
        for row in rows:
            path = row["path"]
            assert path not in files
            raw = prior.read(prefix + path.replace("\\", "/"))
            assert len(raw) == row["bytes"] and sha(raw).lower() == row["sha256"].lower(), path
            files[path] = raw
        old_build = json.loads(files["support\\BUILD-MANIFEST.json"])
        assert old_build["provider_model_bundled"] is False
        assert sha(files["payload\\OptiScaler\\amd_fidelityfx_dx12.dll"]) == old_build["sdk_sha256"]
        assert files["payload\\OptiScaler.ini"] == (REPO / "integration/OptiScaler.ini").read_bytes()

    files["payload\\OptiScaler.dll"] = product
    files["payload\\nvngx.dll_dlssnr.dll"] = forwarder
    files["support\\Collect-NR-Review.ps1"] = (
        REPO / "tools/packaging/customer/support/Collect-NR-Review.ps1"
    ).read_bytes()
    files["support\\NATIVE-MFG-BUILD-RESULT.md"] = (
        REPO / "docs/implementation-evidence/native-mfg/BUILD-RESULT.md"
    ).read_bytes()
    files["support\\NATIVE-MFG-NR-INTEGRATION.md"] = (
        REPO / "docs/implementation-evidence/native-mfg/NR-INTEGRATION.md"
    ).read_bytes()
    files["support\\THIRD_PARTY_MFGAdaUnlock_LICENSE"] = (
        REPO / "OptiScaler/mfg/THIRD_PARTY_MFGAdaUnlock_LICENSE"
    ).read_bytes()
    package_commit = git("rev-parse", "HEAD")
    files["support\\BUILD-MANIFEST.json"] = encoded({
        "status": "built", "commit": package_commit,
        "branch": "codex/native-mfg-experimental",
        "build": {"exit_code": 0, "source_unchanged": True,
                  "evidence": "support\\NATIVE-MFG-NR-INTEGRATION.md"},
        "product_source_commit": SOURCE_BUILD_COMMIT,
        "base_product_commit": "23b1cb299d8e378371ba9eb7753f499d6ae4bc6b",
        "source_package_sha256": BASE_SHA256,
        "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
        "sdk_sha256": old_build["sdk_sha256"],
        "build_evidence": "support\\NATIVE-MFG-NR-INTEGRATION.md",
        "game_scope": "D3D12 RTX 40 series fixed 2X-6X MFG with NR Temporal/Present; HDR/RR combination game trial pending",
        "provider_model_bundled": False,
        "streamline_provider_bundled": False,
    })
    files["READ ME.txt"] = (
        f"{name} - EXPERIMENTAL NATIVE ADA MFG GAME TEST\r\n\r\n"
        "This is NeuRotic's D3D12 RTX 40 series DLSS-G 2X-6X + NR test build.\r\n"
        "It is based on accepted product commit 23b1cb29. The previous MFG build\r\n"
        "worked in EspiOwnage's Crimson Desert trial; this NR/HDR/RR integration needs\r\n"
        "a new game trial. Local checks and limits are in the included report.\r\n\r\n"
        "INSTALL\r\n"
        "1. Extract this whole ZIP outside the game folder and close the game.\r\n"
        "2. Run NeuRotic-Manual-Setup.cmd, choose the game's EXE and a proxy name it loads.\r\n"
        "   For D3D12, dxgi.dll is the usual first choice. Review occupied-file\r\n"
        "   prompts; setup makes verified backups before replacing managed files.\r\n"
        "3. Use a D3D12 game that loads native DLSS-G and nvngx_dlssg.dll.\r\n"
        "   NeuRotic discovers the active sl.dlss_g wrapper, including NVIDIA's\r\n"
        "   OTA override. Setup leaves provider files alone. No fixed hash gate\r\n"
        "   is used; unsupported structures still refuse higher manual ratios.\r\n\r\n"
        "TRY MFG\r\n"
        "1. Start the game yourself. Enable its Frame Generation option at 2X.\r\n"
        "2. In NeuRotic Frame Generation, enable\r\n"
        "   Unlock DLSS MFG above 2X (RTX 40 series).\r\n"
        "   Set Override DLSSG Ratio to Game, Off, or 2X through 6X. The loaded\r\n"
        "   provider determines the available upper limit. Save settings.\r\n"
        "   Close and restart the game with the unlocker saved as enabled so\r\n"
        "   it can prepare MFG before NVIDIA's startup capability query.\r\n"
        "3. Once the active provider is published, check whether the in-game\r\n"
        "   menu offers higher ratios. Compare each choice in a repeatable scene. Note\r\n"
        "   publication status, visible distinct frames, cadence and any artifact.\r\n"
        "   Stop the session if device removal or an uncertain GPU failure occurs.\r\n"
        "4. Enable NR using your existing NR model and try Temporal, then Present.\r\n"
        "   Temporal runs on real source frames, after RR when RR is active.\r\n"
        "   Present uses the final game backbuffer and the existing HDR10/scRGB\r\n"
        "   color path. Keep advanced NR Multipass off with Present + MFG.\r\n"
        "   Use fixed MFG; dynamic mode and UI recomposition are outside this trial.\r\n"
        "   Compare HDR off/on and RR off/on in the same scene. Switch ratios and\r\n"
        "   NR modes, then check motion, color, menus and frame pacing. A refused\r\n"
        "   NR path keeps the original game frames and reports the reason.\r\n"
        "5. Enable Info file logging in Diagnostics if no OptiScaler.log exists.\r\n"
        "   After closing the game, run Collect-NR-Review.cmd. Paste the game EXE\r\n"
        "   folder and return the REVIEW_ZIP it prints. It copies logs/settings\r\n"
        "   and records DLL names, versions and hashes, not provider DLL bytes.\r\n\r\n"
        "RESTORE\r\n"
        "Close the game and run NeuRotic-Manual-Uninstall.cmd or the installed\r\n"
        "Uninstall NeuRotic.cmd. Keep the backup folder until restore completes.\r\n\r\n"
        "Exact build identity is in support\\BUILD-MANIFEST.json and\r\n"
        "support\\PACKAGE-MANIFEST.json. The collector and setup do not run\r\n"
        "the game or load the Streamline providers.\r\n"
    ).encode("utf-8")
    assert not any(path.replace("/", "\\").lower().endswith((
        "\\nvngx_dlssg.dll", "\\sl.dlss_g.dll", "\\nvngx_dlssnr.dll", ".pdb", ".obj", ".lib"
    )) for path in files)
    manifest = {
        "kind": "neurotic-customer-candidate", "lifecycle": "experimental-review",
        "name": name, "branch": "codex/native-mfg-experimental",
        "commit": package_commit, "public_release": False, "deployed": False,
        "runtime_result": "Native Ada MFG offline verification passed; exact game test pending",
        "source_package_sha256": BASE_SHA256,
        "files": [{"path": path, "bytes": len(raw), "sha256": sha(raw)}
                  for path, raw in sorted(files.items())],
    }
    files["support\\PACKAGE-MANIFEST.json"] = encoded(manifest)
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            assert ".." not in path.split("\\") and ":" not in path
            info = zipfile.ZipInfo(f"{name}/{path.replace(chr(92), '/')}")
            info.date_time = (2026, 9, 29, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert len(archive.namelist()) == len(files)
        for path, raw in files.items():
            assert archive.read(f"{name}/{path.replace(chr(92), '/')}") == raw, path
    print(json.dumps({"zip": str(output), "sha256": sha(output.read_bytes()),
                      "bytes": output.stat().st_size, "members": len(files),
                      "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
                      "package_commit": package_commit}, indent=2))


if __name__ == "__main__":
    main()
