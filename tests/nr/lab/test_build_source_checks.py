import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("build_source_checks", ROOT / "tools/nr/lab/build_source_checks.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class SourceChecksBuildTests(unittest.TestCase):
    def test_registry_uses_all_current_explicit_jobs_and_report_arguments(self):
        registry = builder.load_registry(ROOT)
        jobs = builder.make_jobs(registry)
        self.assertEqual(49, len(jobs))
        self.assertEqual(["source-" + name for name, _ in registry.TESTS], [job["id"] for job in jobs])
        for (name, _), job in zip(registry.TESTS, jobs):
            self.assertEqual(["{run_id}"] if name in registry.REPORT_ARGUMENT else [], job["arguments"])
            self.assertEqual("source", job["kind"])
            self.assertEqual(90, job["timeout_seconds"])

    def test_audited_cpu_suites_can_run_after_gpu_failure(self):
        jobs = {job["id"]: job for job in builder.make_jobs(builder.load_registry(ROOT))}
        for name in ("contracts", "encode_binding", "opaque_sr_evaluation_receipt", "sample_planner", "sample_scope",
                     "schema", "source_enrollment", "source_owner", "stage_contracts", "stage_schema",
                     "native_same_recording", "native_session"):
            self.assertFalse(jobs["source-" + name]["gpu"], name)
        self.assertTrue(jobs["source-native_recording_state"]["gpu"])
        self.assertTrue(jobs["source-owned_continuation"]["gpu"])

    def test_unreviewed_suite_stays_gpu_conservative(self):
        registry = SimpleNamespace(TESTS=(("new_case", "spectre/new_case.cpp"),), REPORT_ARGUMENT=frozenset())
        self.assertTrue(builder.make_jobs(registry)[0]["gpu"])

    def test_changed_cpu_source_fingerprint_stays_gpu_conservative(self):
        registry = builder.load_registry(ROOT)
        from unittest.mock import patch
        with patch.object(builder, "source_fingerprint", return_value="0" * 64):
            self.assertTrue(all(job["gpu"] for job in builder.make_jobs(registry)))

    def test_snapshot_contains_include_closure_and_static_detours_only(self):
        registry = builder.load_registry(ROOT)
        paths = builder.source_paths(ROOT, registry)
        self.assertIn("OptiScaler/library/detours/detours.lib", paths)
        self.assertIn("OptiScaler/nr/orchestration/StreamCoordinatorKernel.cpp", paths)
        self.assertIn("OptiScaler/dlssnr/NrGpuSafety.cpp", paths)
        self.assertIn("tests/nr/finalizer/finalizer_fixture.h", paths)
        self.assertFalse(any(Path(path).suffix.lower() in (".dll", ".exe") for path in paths))
        self.assertFalse(any("private" in path.lower().split("/") for path in paths))

    def test_compile_uses_static_runtime_optimization_and_assertions(self):
        command = builder.common_command(Path("cl.exe"), Path("snapshot"))
        for flag in ("/MT", "/O2", "/UNDEBUG", "/std:c++20", "/DNR_ORCH_INIT_TESTING", "/DNR_LAB_WARP_ONLY"):
            self.assertIn(flag, command)
        self.assertNotIn("/DNDEBUG", command)
        self.assertNotIn("/MD", command)

    def test_frozen_sources_detect_tampering(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            (source / "file.h").write_bytes(b"before")
            hashes = {"file.h": builder.sha(source / "file.h")}
            builder.verify_snapshot(source, hashes)
            (source / "file.h").write_bytes(b"after")
            with self.assertRaisesRegex(ValueError, "changed"):
                builder.verify_snapshot(source, hashes)

    def test_registry_summary_parser_remains_canonical(self):
        registry = builder.load_registry(ROOT)
        self.assertEqual([{"checks": 5, "failures": 0, "raw": "5 checks, 0 failures"}], registry.summaries("5 checks, 0 failures"))
        self.assertTrue(registry.summary_is_valid(registry.summaries("5 checks, 0 failures")))
        self.assertFalse(registry.summary_is_valid(registry.summaries("no summary")))


if __name__ == "__main__":
    unittest.main()
