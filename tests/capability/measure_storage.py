"""Read-only workspace intermediate inventory; never follow junctions/reparse points."""
import os, pathlib, json, sys, collections
root=pathlib.Path(sys.argv[1]); totals=collections.Counter(); count=collections.Counter()
intermediates={'.obj','.pch','.ilk','.ipdb','.iobj','.idb'}
skip={'.git','references','_exports','.codex','.agents'}
def scan(path,checkout):
    try:
        with os.scandir(path) as entries:
            for entry in entries:
                info=entry.stat(follow_symlinks=False)
                if getattr(info,'st_file_attributes',0)&0x400: continue
                if entry.is_dir(follow_symlinks=False):
                    if entry.name not in skip: scan(entry.path,checkout or entry.name)
                elif pathlib.Path(entry.name).suffix.lower() in intermediates:
                    totals[checkout]+=info.st_size; count[checkout]+=1
    except (PermissionError,FileNotFoundError): pass
scan(root,'')
data={'root':str(root),'intermediate_bytes':sum(totals.values()),'by_checkout':dict(totals),'files':dict(count),'junctions_followed':False}
print(json.dumps(data,indent=2))
