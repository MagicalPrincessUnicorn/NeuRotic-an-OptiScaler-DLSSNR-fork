"""Changed-files-only update over the exact installed consolidated flagship."""
import argparse, hashlib, importlib.util, json, subprocess, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "builds/hub/bin"
BASE = ROOT.parent / "_exports/NeuRotic-Current-Flagship-4134edeb.zip"
BASE_SHA = "2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67"
SUPPORT = ROOT / "tools/packaging/customer/support"
EVIDENCE = ROOT / "docs/implementation-evidence/app-focused-ux-20261003"
def sha(raw): return hashlib.sha256(raw).hexdigest()
def encode(value): return (json.dumps(value, indent=2)+"\n").encode("utf-8")
def git(*args): return subprocess.check_output(["git","-C",str(ROOT),*args],text=True).strip()
def read(path):
 path=Path(path).absolute()
 for parent in (path,*path.parents):
  if parent.exists() and (parent.is_symlink() or parent.is_junction()): raise RuntimeError("Linked package input")
 return path.read_bytes()
def inventory(files,windows=False):
 return [{"path":n.replace("/","\\") if windows else n,"bytes":len(raw),"sha256":sha(raw)} for n,raw in sorted(files.items())]
def main():
 parser=argparse.ArgumentParser();parser.add_argument("--output",required=True);parser.add_argument("--preview",action="store_true");parser.add_argument("--source-commit",required=True);args=parser.parse_args()
 source_commit=git("rev-parse",args.source_commit+"^{commit}")
 subprocess.check_call(["git","-C",str(ROOT),"merge-base","--is-ancestor",source_commit,"HEAD"])
 output=Path(args.output).absolute()
 if output.exists(): raise RuntimeError("Preserve the previous delivery")
 assert sha(read(BASE))==BASE_SHA
 with zipfile.ZipFile(BASE) as z:
  assert z.testzip() is None
  old={n.removeprefix("NeuRotic/"):z.read(n) for n in z.namelist()}
  base_manifest=json.loads(old["ARCHIVE-MANIFEST.json"])
  assert {r["path"] for r in base_manifest["files"]}==set(old)-{"ARCHIVE-MANIFEST.json"}
  for row in base_manifest["files"]:assert sha(old[row["path"]])==row["sha256"] and len(old[row["path"]])==row["bytes"]
 desired=dict(old);desired["NeuRotic.Hub.exe"]=read(BIN/"NeuRotic.Hub.exe")
 for folder in ("support/","packages/current/support/"):
  for source in SUPPORT.iterdir():
   if folder+source.name in desired or source.name in ("NeuRotic-RuntimeFiles.ps1","NeuRotic-HubHistory.ps1","NeuRotic-HubSanitize.ps1","NeuRotic-HubDumbfire.ps1"):desired[folder+source.name]=read(source)
 for folder in ("","packages/current/"):
  for before,after in (("NeuRotic-Setup.cmd","NeuRotic-Manual-Setup.cmd"),("NeuRotic-Uninstall.cmd","NeuRotic-Manual-Uninstall.cmd")):
   raw=desired.pop(folder+before)
   if folder:raw=read(ROOT/"tools/packaging/customer"/after)
   else:raw=raw.replace(before.encode(),after.encode()).replace(b"NeuRotic Setup",b"NeuRotic Manual Setup").replace(b"NeuRotic Uninstall",b"NeuRotic Manual Uninstall")
   desired[folder+after]=raw
 for name,raw in list(desired.items()):
  if not name.startswith(("Source/","verification/")) and name.endswith((".txt",".md")) and not name.startswith("packages/current/payload/"):
   desired[name]=raw.replace(b"NeuRotic-Setup.cmd",b"NeuRotic-Manual-Setup.cmd").replace(b"NeuRotic-Uninstall.cmd",b"NeuRotic-Manual-Uninstall.cmd").replace(b"NEUROTIC HUB",b"NEUROTIC").replace(b"NeuRotic Hub",b"NeuRotic")
 for path in (BIN/"Tools/WindowWorker/DepthAnythingSmall").iterdir():
  if path.is_file():desired["Tools/WindowWorker/DepthAnythingSmall/"+path.name]=read(path)
 # Preserve every unchanged renderer/discovery/worker byte and current support seal.
 prefix="packages/current/";inner={n[len(prefix):]:raw for n,raw in desired.items() if n.startswith(prefix) and n!=prefix+"support/PACKAGE-MANIFEST.json"}
 document=json.loads(old[prefix+"support/PACKAGE-MANIFEST.json"]);document["files"]=inventory(inner,True);document["app_support_commit"]=source_commit
 desired[prefix+"support/PACKAGE-MANIFEST.json"]=encode(document)
 registry=json.loads(old["support/Hub-Packages.json"]);registry["packages"][0]["manifestSha256"]=sha(desired[prefix+"support/PACKAGE-MANIFEST.json"])
 desired["support/Hub-Packages.json"]=encode(registry)
 changed_sources=set(git("diff","--name-only","51354c9f","--","apps/NeuRoticHub","tests/hub","tools/hub","tools/packaging/customer").splitlines())
 changed_sources.update(git("ls-files","--others","--exclude-standard","apps/NeuRoticHub","tests/hub","tools/hub","tools/packaging/customer").splitlines())
 for name in sorted(changed_sources):
  source=ROOT/name
  if source.is_file() and source.suffix.lower() in (".cpp",".h",".cs",".ps1",".cmd",".py",".vcxproj",".txt"):
   desired["Source/Hub/"+name]=read(source)
  elif not source.exists():desired.pop("Source/Hub/"+name,None)
 sealed={n:raw for n,raw in desired.items() if n!="ARCHIVE-MANIFEST.json"}
 receipt=dict(base_manifest);receipt.update(hub_source_commit=source_commit,hub_sha256=sha(desired["NeuRotic.Hub.exe"]),app_branch=git("branch","--show-current"),app_update_base_sha256=BASE_SHA,files=inventory(sealed))
 desired["ARCHIVE-MANIFEST.json"]=encode(receipt)
 rows=[]
 for name in sorted(set(old)|set(desired)):
  if old.get(name)==desired.get(name):continue
  assert not name.startswith("/") and ":" not in name and ".." not in name.split("/")
  assert Path(name).name.lower()!="nvngx_dlssnr.dll"
  raw=desired.get(name)
  rows.append({"path":name,"action":"put" if raw is not None else "remove","sha256":sha(raw) if raw is not None else None,"previousSha256":sha(old[name]) if name in old else None,"bytes":len(raw) if raw is not None else 0})
 assert rows and not any(r["path"].startswith("packages/current/payload/") for r in rows)
 # These published runtime files remain byte-for-byte matched with the baseline.
 dependencies=[]
 for name,raw in old.items():
  if name.startswith("discovery/") and not name.endswith(".pdb") or name in ("Tools/WindowWorker/NeuRotic.WindowWorker.exe","Tools/WindowWorker/nvngx.dll_dlssnr.dll","packages/current/payload/OptiScaler.dll","packages/current/payload/nvngx.dll_dlssnr.dll"):
   assert desired[name]==raw;dependencies.append({"path":name,"sha256":sha(raw)})
 identity=sha(encode(rows))[:16]
 manifest={"kind":"neurotic-app-delta","schemaVersion":1,"id":identity,"sourceCommit":source_commit,"baseArchiveSha256":BASE_SHA,"files":rows,"dependencies":dependencies}
 if not args.preview:
  build=json.loads(read(EVIDENCE/"native-build.json"));assert build["exitCode"]==0 and build["sourceCommit"]==source_commit and build["hubSha256"]==sha(desired["NeuRotic.Hub.exe"])
  for row in build["inputs"]:assert sha(read(ROOT/row["path"]))==row["sha256"],row["path"]
  assert b"PASS suite All" in read(EVIDENCE/"all-tests.log")
  assert b"PASS packaged App delta" in read(EVIDENCE/"delta-compatibility.log")
  verified=json.loads(read(EVIDENCE/"delta-compatibility.json"));assert verified["id"]==identity and verified["manifestSha256"]==sha(encode(manifest))
  assert not git("status","--porcelain"),"Final delivery requires the recorded source/evidence commit"
 files={"APP-UPDATE-MANIFEST.json":encode(manifest),"Apply-AppUpdate.ps1":read(ROOT/"tools/hub/Apply-AppUpdate.ps1"),"NeuRotic-AppUpdateSafety.cs":read(ROOT/"tools/hub/NeuRotic-AppUpdateSafety.cs")}
 for row in rows:
  if row["action"]=="put":files["NeuRotic/"+row["path"]]=desired[row["path"]]
 for name,option in (("Apply NeuRotic App Update.cmd",""),("Rollback NeuRotic App Update.cmd"," -Rollback")):
  files[name]=(f'@echo off\r\nsetlocal\r\ntitle NeuRotic Application Update\r\nset "PSModulePath=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\Modules"\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-AppUpdate.ps1"{option} %*\r\nset "result=%ERRORLEVEL%"\r\npause\r\nexit /b %result%\r\n').encode("ascii")
 files["READ FIRST.txt"]=b"NEUROTIC APPLICATION UPDATE\r\n\r\nClose NeuRotic. Extract this update into a separate folder. Run Apply NeuRotic App Update.cmd and choose NeuRotic.Hub.exe in your installed consolidated flagship folder.\r\nThe updater checks the matching dependencies, preserves originals for rollback, and changes only listed application files. Settings and private NR/Streamline files are preserved.\r\nOpen NeuRotic.Hub.exe as usual after the update. Runtime files migrate to LocalAppData/NeuRotic/Hub/Runtime and survive future app releases.\r\nDepth Anything V2 Small and its matching licensed runtime are included. Your private NVIDIA NR file remains supplied by you.\r\nRollback NeuRotic App Update.cmd restores the previous application files.\r\nFor manual copying, merge the included NeuRotic folder into your existing app folder and remove only the two superseded NeuRotic-Setup.cmd/NeuRotic-Uninstall.cmd launchers (also in packages/current). The CMD updater performs these renames safely.\r\n"
 output.parent.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(output,"x",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name,raw in sorted(files.items()):z.writestr(name,raw)
 with zipfile.ZipFile(output) as z:
  assert z.testzip() is None
  for name,raw in files.items():assert z.read(name)==raw
 print(json.dumps({"archive":str(output),"sha256":sha(read(output)),"bytes":output.stat().st_size,"changedFiles":len(rows),"members":len(files),"id":identity,"sourceCommit":source_commit,"hubSha256":sha(desired["NeuRotic.Hub.exe"]),"matchingBaselineDependencies":len(dependencies)},indent=2))
if __name__=="__main__":main()
