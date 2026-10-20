"""Native ABI/lifecycle mutations and bounded CPU/WARP execution; no provider run."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[3]
NATIVE = ROOT / "tools/nr/lab/streamline/control/native"


class NativeSourceTests(unittest.TestCase):
    def test_init_boundary_returns_before_device(self):
        source=(NATIVE/'StreamlineNrControl.cpp').read_text()
        self.assertIn('INIT_CONSENT_MISMATCH',source)
        self.assertIn('if(a.mode=="init")return;',source)
        self.assertLess(source.index('if(a.mode=="init")return;'),source.index('D3D12CreateDevice('))
        modules=(NATIVE/'Modules.h').read_text()
        self.assertIn('resident_at_checkpoint',modules)
        self.assertIn('backing_file_at_checkpoint',modules)
        self.assertIn('nvngx',modules)

    def test_failed_support_preserves_plugin_diagnostics_before_abort(self):
        source = (NATIVE / "StreamlineNrControl.cpp").read_text()
        self.assertIn('p.logMessageCallback=&SupportLog', source)
        self.assertIn('p.logLevel=sl::LogLevel::eVerbose', source)
        self.assertIn('p.pathToLogsAndData=dir.c_str()', source)
        self.assertIn('modules->Check(*package,t,"after_init")', source)
        self.assertIn('modules->Check(*package,t,"after_device")', source)
        self.assertIn('CaptureSupportDiagnostics(t,api)', source)
        self.assertLess(source.index('CaptureSupportDiagnostics(t,api)'),source.index('Ok(supportResult)'))
        self.assertIn('modules->Check(*package,t,"after_support_failure")', source)

    def test_default_off_explicit_mode_and_consent(self):
        source = (NATIVE / "StreamlineNrControl.cpp").read_text()
        self.assertIn('"EXPLICIT_MODE_REQUIRED"', source)
        self.assertIn('"PHYSICAL_CONSENT_MISMATCH"', source)
        self.assertIn('"SUPERVISED_JOB_REQUIRED"', source)

    def test_existing_submission_and_dred_owners(self):
        source = (NATIVE / "StreamlineNrControl.cpp").read_text()
        self.assertIn("stage_probe::Submit(d,q,l,f,value,o)", source)
        self.assertIn("ConfigureDred(receipt)", source)
        self.assertNotIn("ExecuteCommandLists(", source)
        self.assertNotIn("NVSDK_NGX_D3D12_", source)

    def test_no_ota_or_version_retry(self):
        source = (NATIVE / "StreamlineNrControl.cpp").read_text()
        self.assertIn("api.init(p,sl::kSDKVersion)", source)
        self.assertNotIn("eAllowOTA", source)
        self.assertNotIn("eLoadDownloadedPlugins", source)
        self.assertEqual(source.count("api.init("), 1)

    def test_readback_preserved_before_attribution(self):
        source = (NATIVE / "StreamlineNrControl.cpp").read_text()
        self.assertLess(source.index('dir/L"output.rgba16f"'), source.index("Attribute(pixels,"))
        self.assertLess(source.index('dir/L"depth.r32f"'), source.index("Attribute(pixels,"))

    def test_loader_callback_no_io_or_allocation(self):
        source = (NATIVE / "Modules.h").read_text()
        callback = source.split("static void CALLBACK Callback", 1)[1].split("void Start", 1)[0]
        for forbidden in ("new ", "CreateFile", "Hash(", "lock_guard", ".Emit", "LoadLibrary"):
            self.assertNotIn(forbidden, callback)
        self.assertIn("InterlockedExchange", callback)


@unittest.skipUnless(os.environ.get("NR_CONTROL_NATIVE_BINARY"), "set NR_CONTROL_NATIVE_BINARY to the locked native build")
class NativeExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = Path(os.environ["NR_CONTROL_NATIVE_BINARY"]).resolve()
        cls.output = Path(os.environ.get("NR_CONTROL_TEST_OUTPUT", str(cls.binary.parent / "native-integration-tests")))
        cls.output.mkdir(parents=True, exist_ok=True)

    def run_mode(self, mode, extra=(), nonce=None):
        folder = self.output / (mode + "-" + uuid.uuid4().hex)
        folder.mkdir()
        trace = folder / "trace.jsonl"
        command = [str(self.binary), "--mode", mode, "--package", str(folder / "unused-provider"),
                   "--manifest", str(folder / "unused-manifest"), "--trace", str(trace),
                   "--nonce", nonce or uuid.uuid4().hex, *extra]
        process = subprocess.run(command, capture_output=True, text=True, timeout=45)
        (folder / "stdout.txt").write_text(process.stdout)
        (folder / "stderr.txt").write_text(process.stderr)
        (folder / "invocation.json").write_text(json.dumps({"command": command, "exit_code": process.returncode}, indent=2))
        events = [json.loads(row) for row in trace.read_text().splitlines()] if trace.exists() else []
        return process, events, folder

    def test_cpu_contract_mutations(self):
        process, events, _ = self.run_mode("cpu")
        self.assertEqual(process.returncode, 0, process.stderr)
        tests = [row["data"] for row in events if row["event"] == "contract_test"]
        self.assertGreaterEqual(len(tests), 37)
        self.assertTrue(all(row["passed"] for row in tests))
        self.assertEqual(len({row["name"] for row in tests}), len(tests))
        self.assertFalse(events[-1]["data"]["provider_evaluated"])
        self.assertEqual([row["seq"] for row in events], list(range(len(events))))

    def test_warp_independent_readback(self):
        process, events, folder = self.run_mode("warp")
        self.assertEqual(process.returncode, 0, process.stderr)
        expected = struct.pack("<4H", 0x3400, 0x3800, 0x3A00, 0x3C00) * 4096
        for name in ("output.rgba16f", "consumer.rgba16f", "input.rgba16f", "expected-copy.rgba16f"):
            self.assertEqual((folder / name).read_bytes(), expected)
        self.assertEqual((folder / "guard.rgba16f").read_bytes(), bytes(32768))
        self.assertEqual((folder / "depth.r32f").read_bytes(), struct.pack("<f", 1.0) * 4096)
        self.assertEqual((folder / "motion.rg16f").read_bytes(), bytes(16384))
        readback = next(row["data"] for row in events if row["event"] == "readback")
        self.assertEqual(readback["sha256"], hashlib.sha256(expected).hexdigest())
        stages = [row["data"] for row in events if row["event"] == "lifecycle" and row["data"]["phase"] == "completed"]
        self.assertEqual([s["completed_fence"] for s in stages], [1, 2, 3])
        terminal = events[-1]["data"]
        self.assertTrue(terminal["gpu_completed"] and terminal["output_attribution"])
        self.assertFalse(terminal["control_pass"] or terminal["provider_evaluated"])

    def test_physical_requires_consent(self):
        process, _, _ = self.run_mode("physical")
        self.assertEqual(process.returncode, 2)
        self.assertIn("PHYSICAL_CONSENT_MISMATCH", process.stderr)

    def test_init_requires_separate_consent(self):
        for extra in ([],['--allow-provider-evaluate']):
            process,_,_=self.run_mode('init',extra)
            self.assertEqual(process.returncode,2)
        process,_,_=self.run_mode('cpu',['--allow-provider-init'])
        self.assertEqual(process.returncode,2)
        self.assertIn('INIT_CONSENT_MISMATCH',process.stderr)

    def test_init_refuses_before_api_without_provider_package(self):
        process,events,_=self.run_mode('init',['--allow-provider-init'])
        self.assertEqual(process.returncode,3)
        # The test harness itself may already be in a Windows job.
        self.assertIn(events[-1]['data']['reason'],('SUPERVISED_JOB_REQUIRED','PATH_UNAVAILABLE'))
        self.assertFalse(any(e['event']=='api_enter' for e in events))

    def test_cpu_cannot_carry_physical_consent(self):
        process, _, _ = self.run_mode("cpu", ["--allow-provider-evaluate"])
        self.assertEqual(process.returncode, 2)
        self.assertIn("PHYSICAL_CONSENT_MISMATCH", process.stderr)

    def test_nonce_shape_rejected(self):
        process, _, _ = self.run_mode("cpu", nonce="bad")
        self.assertEqual(process.returncode, 2)
        self.assertIn("NONCE_REQUIRED", process.stderr)

    def test_default_mode_rejected(self):
        process, _, _ = self.run_mode("default")
        self.assertEqual(process.returncode, 2)
        self.assertIn("EXPLICIT_MODE_REQUIRED", process.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
