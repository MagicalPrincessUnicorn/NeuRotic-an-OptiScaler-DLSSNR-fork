"""Remove only this worker's completed fixture compiler caches; keep stable production cache."""
import os,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
target=(root/'builds/window-worker').resolve()
allowed=(Path('C:/NeuRotic')/'w03-ui-preflight'/'builds'/'window-worker').resolve()
if target!=allowed or not target.is_relative_to(Path('C:/NeuRotic').resolve()):raise RuntimeError('Unexpected cleanup target')
for ancestor in (target,*target.parents):
 if ancestor.exists() and os.stat(ancestor,follow_symlinks=False).st_file_attributes&0x400:raise RuntimeError('Refusing reparse ancestor')
extensions={'.obj','.pch','.ipch','.ilk','.idb','.iobj','.ipdb'}
keep={Path('obj'),Path('nr/forwarder-obj')}
before=removed=0
for base,dirs,files in os.walk(target,followlinks=False):
 dirs[:]=[d for d in dirs if not os.stat(Path(base)/d,follow_symlinks=False).st_file_attributes&0x400]
 for name in files:
  path=Path(base)/name;stat=os.stat(path,follow_symlinks=False)
  if stat.st_file_attributes&0x400 or path.suffix.lower() not in extensions:continue
  before+=stat.st_size;relative=path.relative_to(target)
  if any(relative.is_relative_to(k) for k in keep) or relative.parent==Path('bin'):continue
  if not path.resolve().is_relative_to(target):raise RuntimeError('Escaped cleanup root')
  removed+=stat.st_size;path.unlink()
receipt={'target':str(target),'disposableBeforeBytes':before,'removedCompletedFixtureBytes':removed,'disposableAfterBytes':before-removed,'preservedProductionCaches':['obj','nr/forwarder-obj','bin/*.ilk'],'scope':'Only completed window-worker fixtures; final binaries/PDBs/source/dependencies/receipts preserved'}
(root/'docs/implementation-evidence/window-nr-20261002/worker-storage-cleanup.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(receipt))
