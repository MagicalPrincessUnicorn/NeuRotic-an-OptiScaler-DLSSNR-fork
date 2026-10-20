"""Seal the Windows Vulkan experiment with the existing reversible game installer.

The accepted W03 observations remain bound to their prior binaries. This archive is a
new manual-test candidate; it does not install anything or load a model/provider.
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
BASE = REPO.parent / "_exports/NeuRotic-W03-GameTest-20260928-r32-provider-scope.zip"
BASE_SHA256 = "64ec5518625dfc71161413a8dd1a785cdd1085d7f3d6321129964afd64670002"


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def git(*args: str) -> str:
    return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", type=Path, required=True)
    parser.add_argument("--forwarder", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.parent == (REPO.parent / "_exports").resolve()
    assert re.fullmatch(r"NeuRotic-Vulkan-Experiment-[0-9a-f]{8}", output.stem)
    assert not output.exists(), "An existing delivered archive is never overwritten"
    assert git("branch", "--show-current").startswith("codex/vulkan-experiment")
    assert re.fullmatch(r"[0-9a-f]{40}", args.source_commit)
    assert not git("diff", "--name-only", args.source_commit + "..HEAD", "--", "OptiScaler")
    assert not git("diff", "HEAD", "--name-only", "--", "OptiScaler")
    assert sha(BASE.read_bytes()) == BASE_SHA256

    product = args.product.read_bytes()
    forwarder = args.forwarder.read_bytes()
    assert product[:2] == b"MZ" and forwarder[:2] == b"MZ"
    build_receipt = json.loads((args.product.resolve().parent.parent / "exact-build-receipt.json").read_text(encoding="utf-8-sig"))
    assert build_receipt["source_commit"] == args.source_commit
    assert build_receipt["target"] == "Rebuild" and build_receipt["exit_code"] == 0 and build_receipt["source_unchanged"]
    assert build_receipt["product_sha256"] == sha(product) and build_receipt["forwarder_sha256"] == sha(forwarder)
    ini = (REPO / "integration/OptiScaler.ini").read_bytes()
    ini_text = ini.decode("utf-8-sig")
    assert build_receipt["config_sha256"] == sha(ini)
    shader_receipt = (REPO / "docs/implementation-evidence/vulkan-experiment/VULKAN-SHADERS.json").read_bytes()
    assert build_receipt["shaders_sha256"] == sha(shader_receipt)
    for required in ("NativeProtocol=false", "ExperimentalMode = false",
                     "OverrideFgGuardrails = false", "OverrideHdrGuardrails = false"):
        assert required in ini_text, required
    assert not re.search(r"(?m)^\s*Enabled\s*=\s*true\s*$", ini_text)

    with zipfile.ZipFile(BASE) as prior:
        prefix = prior.namelist()[0].split("/")[0] + "/"
        previous = json.loads(prior.read(prefix + "support/PACKAGE-MANIFEST.json"))
        files: dict[str, bytes] = {}
        for row in previous["files"]:
            path = row["path"]
            assert path not in files
            raw = prior.read(prefix + path.replace("\\", "/"))
            assert len(raw) == row["bytes"] and sha(raw).lower() == row["sha256"].lower(), path
            files[path] = raw
    assert "payload\\OptiScaler.dll" in files
    assert "payload\\nvngx.dll_dlssnr.dll" in files
    assert "support\\NeuRotic-Setup-Engine.ps1" in files
    assert "NeuRotic-Manual-Uninstall.cmd" in files
    assert not any(path.lower().endswith("nvngx_dlssnr.dll") for path in files)

    files["payload\\NeuRotic-LICENSE.txt"] += (
        b"\r\n\r\nThird-party MFG Ada Unlock policy (ImDreamt):\r\n" +
        (REPO / "OptiScaler/mfg/THIRD_PARTY_MFGAdaUnlock_LICENSE").read_bytes())
    files["payload\\OptiScaler.dll"] = product
    files["payload\\nvngx.dll_dlssnr.dll"] = forwarder
    files["payload\\OptiScaler.ini"] = ini
    files["support\\VULKAN-VERIFICATION.md"] = (
        REPO / "docs/implementation-evidence/vulkan-experiment/VERIFICATION.md").read_bytes()
    files["support\\VULKAN-GAME-TEST-HANDOFF.md"] = (
        REPO / "docs/implementation-evidence/vulkan-experiment/GAME-TEST-HANDOFF.md").read_bytes()
    for name in ("PARITY-VERIFICATION-20260930.md", "PARITY-CAPABILITY-LEDGER.md", "PARITY-GAME-TEST-HANDOFF.md", "VULKAN-SHADERS.json"):
        files["support\\" + name] = (REPO / "docs/implementation-evidence/vulkan-experiment" / name).read_bytes()
    files["support\\EXACT-BUILD-RECEIPT.json"] = encoded(build_receipt)
    files["support\\BUILD-MANIFEST.json"] = encoded({
        "status": "built", "candidate": "Windows native Vulkan experiment",
        "commit": git("rev-parse", "HEAD"),
        "build": {"exit_code": 0, "source_unchanged": True,
                  "evidence": "VULKAN-VERIFICATION.md and stable Release/x64 build-product.log"},
        "product_source_commit": args.source_commit,
        "package_source_commit": git("rev-parse", "HEAD"),
        "branch": git("branch", "--show-current"),
        "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
        "source_package_sha256": BASE_SHA256,
        "prior_acceptance_transfers_to_this_binary": False,
        "native_temporal_game_result": "NOT_RUN",
        "present_game_result": "NOT_RUN",
        "provider_model_bundled": False,
        "exact_rebuild": build_receipt,
        "full_parity_claimed": False,
    })
    files["READ ME.txt"] = (
        f"{output.stem} - EXPERIMENTAL WINDOWS VULKAN TEST\r\n\r\n"
        "This candidate extends Native Temporal and Present with live resolution/model changes,\r\n"
        "independent 1-10 passes, fresh guides, HDR recipes, diagnostics and matched captures.\r\n"
        "Both require a new game observation on this exact DLL/forwarder pair.\r\n"
        "Keep frame generation off for the first tests. True PQ/scRGB needs new format evidence.\r\n"
        "Full parity is not yet claimed: read support\\PARITY-CAPABILITY-LEDGER.md for limits.\r\n"
        "NR remains off by default. The private nvngx_dlssnr.dll model is not included.\r\n\r\n"
        "Close the game, extract the full ZIP outside the game directory, and run\r\n"
        "NeuRotic-Manual-Setup.cmd. For a native Vulkan game, winmm.dll is the installer's\r\n"
        "usual proxy choice when the game loads it. Confirm the loaded proxy in logs.\r\n"
        "Choose the game EXE and review any occupied-file prompt. Setup records backups.\r\n"
        "Supply your own private model beside the game EXE before enabling NR.\r\n"
        "Read support\\PARITY-GAME-TEST-HANDOFF.md for the current test matrix.\r\n\r\n"
        "To restore, close the game and run NeuRotic-Manual-Uninstall.cmd or the\r\n"
        "installed Uninstall NeuRotic.cmd. No game is launched by this package.\r\n"
    ).encode("utf-8")
    assert not any(path.lower().endswith((".pdb", ".exp", ".lib")) for path in files)
    manifest = {
        "kind": "neurotic-customer-candidate", "lifecycle": "experimental-review",
        "name": output.stem, "commit": git("rev-parse", "HEAD"),
        "branch": git("branch", "--show-current"),
        "public_release": False, "deployed": False,
        "runtime_result": "Native and Present manual game tests NOT_RUN",
        "decision": "Manual experimental installation only; no promotion",
        "source_package_sha256": BASE_SHA256,
        "provider_model_bundled": False,
        "files": [{"path": path, "bytes": len(raw), "sha256": sha(raw)}
                  for path, raw in sorted(files.items())],
    }
    files["support\\PACKAGE-MANIFEST.json"] = encoded(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            assert ".." not in path.split("\\") and ":" not in path
            info = zipfile.ZipInfo(f"{output.stem}/{path.replace(chr(92), '/')}")
            info.date_time = (2026, 9, 29, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert len(archive.namelist()) == len(files)
        for path, raw in files.items():
            assert archive.read(f"{output.stem}/{path.replace(chr(92), '/')}") == raw
    print(json.dumps({"package": str(output), "bytes": output.stat().st_size,
                      "sha256": sha(output.read_bytes()),
                      "product_sha256": sha(product), "forwarder_sha256": sha(forwarder),
                      "files": len(files), "source_commit": args.source_commit}, indent=2))


if __name__ == "__main__":
    main()
