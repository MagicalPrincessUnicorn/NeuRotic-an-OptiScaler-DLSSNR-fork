"""Package the verified RND009 bridge DLL with the existing reversible installer."""
from pathlib import Path
import argparse, hashlib, json, subprocess, zipfile

REPO = Path(__file__).resolve().parents[3]
BASE = REPO.parent / "_exports/NeuRotic-Native-Ada-MFG-NR-9adfbb45.zip"
BASE_HASH = "74dfc8fe6a4c48a588c9c9cd6ab2836f83277a7e4e18395dc43bc17a1c32fc69"
PRODUCT_COMMIT = "8f4bde9866247eb40a18e71581aedde4f45425bb"
PRODUCT_HASH = "61882f4fab19bd9e2253f459930b2a550133c99b166f8c0930a8653b22154f00"
FORWARDER_HASH = "e19698aecaa53678794578a2dd28ea4dfd8a303837943e57a41d40b6efa13845"
SDK_HASH = "2b2077fd916793dd3ea0e72fc63bdb70e58afcdb650795f06888e470f0160cc5"

def sha(raw): return hashlib.sha256(raw).hexdigest()
def encoded(value): return (json.dumps(value, indent=2) + "\n").encode("utf-8")
def git(*args): return subprocess.check_output(["git", "-C", str(REPO), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    output = parser.parse_args().output.resolve()
    assert output.parent == (REPO.parent / "_exports").resolve()
    assert not output.exists(), "Preserve prior deliveries"
    assert git("branch", "--show-current") == "codex/interop-safety"
    assert not git("diff", "--name-only", PRODUCT_COMMIT + "..HEAD", "--", "OptiScaler")
    assert not git("status", "--porcelain", "--", "OptiScaler"), "Product source must be unchanged"
    receipt = json.loads((REPO / "builds/interop-safety/verified-source.json").read_text(encoding="utf-8"))
    assert receipt["productCommit"] == PRODUCT_COMMIT and receipt["buildExit"] == 0 and receipt["regressionsExit"] == 0
    for path, expected in receipt["sourceHashes"].items():
        assert sha((REPO / path).read_bytes()) == expected, path
    product = (REPO / "builds/interop-safety/release/out/OptiScaler.dll").read_bytes()
    assert sha(product) == PRODUCT_HASH
    assert sha(BASE.read_bytes()) == BASE_HASH
    files = {}
    with zipfile.ZipFile(BASE) as archive:
        prefix = archive.namelist()[0].split("/")[0] + "/"
        for row in json.loads(archive.read(prefix + "support/PACKAGE-MANIFEST.json"))["files"]:
            path = row["path"]
            assert path not in files and ":" not in path and ".." not in path.replace("\\", "/").split("/")
            raw = archive.read(prefix + path.replace("\\", "/"))
            assert len(raw) == row["bytes"] and sha(raw) == row["sha256"].lower(), path
            files[path] = raw
    assert sha(files["payload\\nvngx.dll_dlssnr.dll"]) == FORWARDER_HASH
    assert sha(files["payload\\OptiScaler\\amd_fidelityfx_dx12.dll"]) == SDK_HASH
    for path in ["support\\NATIVE-MFG-BUILD-RESULT.md", "support\\NATIVE-MFG-NR-INTEGRATION.md", "support\\W03-ACCEPTANCE.json"]:
        files.pop(path, None)
    files["payload\\OptiScaler.dll"] = product
    files["payload\\OptiScaler.ini"] = (REPO / "integration/OptiScaler.ini").read_bytes()
    files["support\\Collect-NR-Review.ps1"] = (REPO / "tools/packaging/customer/support/Collect-NR-Review.ps1").read_bytes()
    files["support\\INTEROP-SAFETY-CLOSEOUT.md"] = (REPO / "docs/ai/live/RND009_INTEROP_SAFETY_BUILD_2026-09-29.md").read_bytes()
    files["support\\VERIFIED-SOURCE.json"] = encoded(receipt)
    files["support\\BG3-REPAIR-R2.md"] = (REPO / "docs/ai/live/RND009_BG3_REPAIR_2026-09-30.md").read_bytes()
    instructions = """NeuRotic DX11/DX12 Bridge Repair - 8f4bde98 - BG3 R2 TEST

INSTALL
1. Close the game. Extract the whole ZIP outside the game directory.
2. Run NeuRotic-Manual-Setup.cmd. For BG3, select bin/bg3_dx11.exe and dxgi.dll.
   Setup preserves existing settings and makes verified recovery backups.
   For an existing managed installation, choose Update when prompted.
   Read any occupied-proxy prompt before choosing to replace it.
3. Launch BG3 in DirectX 11 yourself. This package does not launch the game.
   Use your existing nvngx_dlssnr.dll model; no private model is bundled.
4. Enable Info file logging in Diagnostics so OptiScaler.log is captured.

TEST THE CHANGED BRIDGE
1. Enable the game's DLSS input and select a w/Dx12/on12 upscaler in NeuRotic
   (DLSS, XeSS or FSR backed by DX12). Ordinary native DX11 upscaling will not
   test the changed bridge. Verify the actual bridge/backend in diagnostics.
2. Start with NR off, FG off, Multipass off and Pre-DLAA off, in a repeatable
   scene (2-3 minutes). Record FPS and frame-time behavior.
3. Enable NR and select Native Temporal. Compare the same scene (3-5 minutes).
   Confirm actual model evaluation/composition, not merely Enabled status.
   For this repair baseline, use Before placement and Match Game Render.
   Keep the same resolution and upscale quality. Record FPS and frame timing.
   Keep FG, Multipass and Pre-DLAA off for this first comparison.
   Pre-DLAA has a separate legacy binding path not repaired in this candidate.
4. Change upscale quality and resolution; alt-tab and return; load another
   save; return to the main menu; exit and relaunch. Note persistent refusal,
   black/stale frames, flicker, corruption, crashes or frame-time changes.
5. After this baseline, optionally repeat with an available supported FG path.
   Record its exact provider and setting; unavailable FG is not test evidence.
   Stop and preserve diagnostics if a device removal/GPU failure occurs.
6. Close the game. Run Collect-NR-Review.cmd and paste its EXE directory.
   Return the printed REVIEW_ZIP with game/API, backend, NR placement, FG,
   resolution, driver, test sequence and visible result. Include a screenshot
   of bridge status if possible. The collector uploads nothing.

OTHER TEST CANDIDATES (this exact build is not game-qualified yet)
God of War (2018 PC): a second DX11 engine with DLSS inputs. Use the game's
DLSS input, not FSR2 input. Test combat/camera motion and load transitions.
https://github.com/optiscaler/OptiScaler/wiki/God-of-War-2018
Crysis 3 Remastered: DX11, DLSS inputs, foliage/motion stress. Start with
Quality/Balanced, not Native AA/DLAA (upstream requires upscale ratio >1.0).
https://github.com/optiscaler/OptiScaler/wiki/Crysis-3-Remastered
Use the same w/Dx12 backend and Native Temporal selection in each game.

RESTORE
Close the game and run NeuRotic-Manual-Uninstall.cmd or the installed
Uninstall NeuRotic.cmd. Keep recovery backups until restore completes.

BUILD IDENTITY
Product source: 8f4bde9866247eb40a18e71581aedde4f45425bb.
R2 repairs the bridge Pre-SR pointer binding and adds bounded assistance for
its known submitted work. Safety proofs and unknown-work refusal remain.
Release build and deterministic CPU/WARP regression evidence is included.
Real game/model/provider, image quality and performance qualification is pending.
An older cross-game script has a baseline Present regex failure, documented in
support/BG3-REPAIR-R2.md; the focused bridge contracts passed.
The forwarder and SDK are reused unchanged from the accepted native-MFG package.
No Vulkan external transport or new compatibility promise is included.
"""
    files["READ ME.txt"] = instructions.replace("\n", "\r\n").encode("utf-8")
    files["support\\TESTING.txt"] = files["READ ME.txt"]
    commit = git("rev-parse", "HEAD")
    files["support\\BUILD-MANIFEST.json"] = encoded({
        "status": "built", "commit": commit, "branch": "codex/interop-safety",
        "product_source_commit": PRODUCT_COMMIT, "base_product_commit": receipt["base"],
        "build": {"exit_code": 0, "source_unchanged": True, "evidence": "support\\VERIFIED-SOURCE.json"},
        "source_package_sha256": BASE_HASH, "product_sha256": PRODUCT_HASH,
        "forwarder_sha256": FORWARDER_HASH, "sdk_sha256": SDK_HASH,
        "provider_model_bundled": False, "streamline_provider_bundled": False,
        "game_scope": "BG3 R2 DX11-to-DX12 bridge repair trial; exact game qualification pending"})
    assert not any(p.replace("/", "\\").lower().endswith(("\\nvngx_dlssnr.dll", "\\nvngx_dlssg.dll", "\\sl.dlss_g.dll", ".obj", ".pch", ".pdb")) for p in files)
    name = output.stem
    manifest = {"kind": "neurotic-customer-candidate", "lifecycle": "experimental-review",
        "name": name, "branch": "codex/interop-safety", "commit": commit,
        "public_release": False, "deployed": False, "runtime_result": "exact game trial pending",
        "source_package_sha256": BASE_HASH,
        "files": [{"path": p, "bytes": len(raw), "sha256": sha(raw)} for p,raw in sorted(files.items())]}
    files["support\\PACKAGE-MANIFEST.json"] = encoded(manifest)
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            info = zipfile.ZipInfo(name + "/" + path.replace("\\", "/"))
            info.date_time = (2026,9,30,0,0,0)
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert len(archive.namelist()) == len(files)
        for p,raw in files.items(): assert archive.read(name + "/" + p.replace("\\", "/")) == raw, p
    print(json.dumps({"zip": str(output), "sha256": sha(output.read_bytes()),
        "bytes": output.stat().st_size, "members": len(files), "product_sha256": PRODUCT_HASH}, indent=2))

if __name__ == "__main__": main()
