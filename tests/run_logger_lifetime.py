"""Provider-free regression: compile production Logger.cpp; run shutdown cases."""
import argparse
import os
import shutil
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=Path("C:/NeuRotic/builds/w03-closure-fixes/logger"))
    parser.add_argument("--label", default="verification")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    before = sum(p.stat().st_size for p in build.iterdir() if p.is_file())
    vcvars = Path("C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat")
    env_lines = subprocess.check_output(
        f'"{vcvars}" -vcvars_ver=14.44 >nul && set', shell=True, text=True
    )
    env = os.environ.copy()
    env.update(line.split("=", 1) for line in env_lines.splitlines() if "=" in line and not line.startswith("="))
    project = ET.parse(repo / "OptiScaler/OptiScaler.vcxproj")
    includes = next(e.text for e in project.iter() if e.tag.endswith("IncludePath") and "spdlog" in (e.text or ""))
    includes = includes.replace("$(ProjectDir)", str(repo / "OptiScaler") + "/").replace("$(SolutionDir)", str(repo) + "/")
    compiler = shutil.which("cl.exe", path=next(v for k, v in env.items() if k.upper() == "PATH"))
    if compiler is None:
        raise RuntimeError("MSVC cl.exe was not found after vcvars64")
    command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/MD", "/O2", "/Gy", "/DNDEBUG", "/DUNICODE", "/D_UNICODE"]
    command += ["/I" + item for item in includes.split(";") if "$(" not in item]
    command += [str(repo / "OptiScaler/Logger.cpp"), str(repo / "tests/logger_lifetime.cpp"),
                "/Fe:" + str(build / "logger_lifetime.exe"), "/link", "/OPT:REF", "/INCREMENTAL:NO"]
    compile_result = subprocess.run(command, cwd=build, env=env, capture_output=True, text=True)
    output = compile_result.stdout + compile_result.stderr
    code = compile_result.returncode
    if code == 0:
        for mode in ("sync", "async", "concurrent"):
            result = subprocess.run([str(build / "logger_lifetime.exe"), mode], cwd=build,
                                    capture_output=True, text=True, timeout=30)
            output += result.stdout + result.stderr + f"{mode} exit={result.returncode}\n"
            code = code or result.returncode
    # Only this harness's own intermediate files, never other build caches.
    for name in ("Logger.obj", "logger_lifetime.obj"):
        (build / name).unlink(missing_ok=True)
    after = sum(p.stat().st_size for p in build.iterdir() if p.is_file())
    output += f"storage before={before} after={after} bytes; own compiler objects removed\n"
    (build / (args.label + ".log")).write_text(output, encoding="utf-8")
    print(output, end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
