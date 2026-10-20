"""Prepare actual old modern publication-cache body, without native execution."""
from pathlib import Path
import sys,re,hashlib,json,subprocess
root=Path(__file__).resolve().parents[3];out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
p=root/'OptiScaler/hooks/Streamline_Hooks.cpp';anchor='44729b64f38ae54c9644cf091df2e170eb7778e7'
baseline=subprocess.check_output(['git','--no-optional-locks','show',anchor+':OptiScaler/hooks/Streamline_Hooks.cpp'],cwd=root)
source=baseline.decode('utf-8');start=source.index('void StreamlineHooks::hookSystemCaps(')
body=re.search(r'if \(!systemCaps\)\s+sl::param::getPointerParam\(params, sl::param::common::kSystemCaps, &systemCaps\);',source[start:]).group(0)
target=out/'cached_caps_reader.inc';target.write_text('static Caps* cachedCaps=nullptr;\ninline Caps* ReadCachedCaps(sl::param::IParameters* params){\n'+body.replace('systemCaps','cachedCaps')+'\nreturn cachedCaps;\n}\n',encoding='utf-8')
receipt=dict(source=str(p),anchor=anchor,sha256=hashlib.sha256(baseline).hexdigest(),method='Exact committed pre-repair modern cache-once branch; only storage identifier renamed for authored Caps fixture. This preserves baseline reproduction after the modern cache is removed.',
 generated=dict(path=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(out/'CACHED-READER-PREPARATION.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print('Prepared actual cached pointer reader; no native execution.')
