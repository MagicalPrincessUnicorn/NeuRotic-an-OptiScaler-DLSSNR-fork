from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
rel='OptiScaler/hooks/Streamline_Hooks.cpp';base='ef238c09b6346ea9dc787a7a157219b8d19ca88a'
raw=subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root);text=raw.decode('utf-8')
start=text.index('uint32_t StreamlineHooks::getSystemCapsArch(')
opening=text.index('if (caps)',start);brace=text.index('{',opening);end=brace+1;depth=1
while depth:
    if text[end]=='{':depth+=1
    if text[end]=='}':depth-=1
    end+=1
body=text[opening:end]
assert body.count('for (auto& adapter : caps->adapters)')==1
assert 'gpuCount' not in body
code='static std::optional<std::uint32_t> ReadArchitecture(const Caps* caps)\n{\n    uint32_t highestArch=0;\n'+body+'\n    return highestArch;\n}\n'
p=out/'old_caps_architecture_reader.inc';p.write_text(code,encoding='utf-8')
print(json.dumps(dict(base=base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual modern if(caps) branch verbatim; caller hardware selection and separate zero fallback outside this pure reader control. No private ABI.')))
