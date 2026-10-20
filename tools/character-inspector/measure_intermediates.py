"""Measure disposable compiler files without following junctions or deleting files."""
import json
import os
from pathlib import Path
import sys

root=Path('C:/NeuRotic')
extensions={'.obj','.pch','.ilk','.iobj','.ipdb','.idb','.o','.gch','.tlog'}
totals={}
errors=[]
for directory,dirs,files in os.walk(root,followlinks=False,onerror=lambda e:errors.append(str(e))):
    # Windows junctions are reparse points, even when is_symlink() is false.
    dirs[:]=[d for d in dirs if d not in ('.git','node_modules') and
             not (Path(directory,d).stat().st_file_attributes & 0x400)]
    for name in files:
        if Path(name).suffix.lower() not in extensions:
            continue
        path=Path(directory,name)
        info=path.stat()
        if info.st_file_attributes & 0x400:
            continue
        owner=str(path.relative_to(root).parts[0])
        totals[owner]=totals.get(owner,0)+info.st_size
receipt={'root':str(root),'bytes':sum(totals.values()),'gib':sum(totals.values())/2**30,
         'by_checkout':totals,'scope':'compiler intermediates by extension; no junction traversal',
         'errors':errors,'deleted_bytes':0}
if len(sys.argv)>1:
    Path(sys.argv[1]).write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,indent=2))
