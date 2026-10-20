from pathlib import Path
import hashlib,json,re,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
path=root/'OptiScaler/hooks/Streamline_Hooks.h';raw=path.read_bytes()
matches=re.findall(r'typedef\s+[^;\n]*\(\*PFN_setVoid\)[^;\n]*;',raw.decode('utf-8'))
assert len(matches)==1,'Expected one actual product declaration'
declaration=matches[0].replace('PFN_setVoid','SourceSetPointer')
out.mkdir(parents=True,exist_ok=True)
p=out/'source_set_pointer_type.inc'
p.write_text('// Exact actual product typedef, identifier renamed only.\n'+declaration+'\n',encoding='utf-8')
print(json.dumps(dict(source=str(path),sha256=hashlib.sha256(raw).hexdigest(),declaration=matches[0],generated=str(p),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest())))
