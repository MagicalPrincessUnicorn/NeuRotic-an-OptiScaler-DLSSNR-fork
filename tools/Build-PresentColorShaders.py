"""Regenerate embedded FXC bytecode for Present HDR and shared composition."""
from pathlib import Path
import subprocess,hashlib,json
root=Path(__file__).resolve().parents[1]
out=root/'.superpowers/sdd/2026-09-28-present-hdr-color';out.mkdir(parents=True,exist_ok=True)
fxc=Path('C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe')
entries=[('format_transfer','PresentColor','Decode','PresentColorDecode'),('format_transfer','PresentColor','Encode','PresentColorEncode')]
receipts=[];header='#pragma once\n'
for folder,source,entry,name in entries:
 src=root/f'OptiScaler/shaders/{folder}/precompile/{source}.hlsl';dst=out/(name+'.cso')
 subprocess.run([str(fxc),'/nologo','/T','cs_5_0','/E',entry,'/Ges','/WX','/O3','/Fo',str(dst),str(src)],check=True)
 b=dst.read_bytes();header+='inline constexpr unsigned char '+name+'_cso[] = {\n'+'\n'.join('    '+','.join(f'0x{x:02x}' for x in b[i:i+16])+',' for i in range(0,len(b),16))+'\n};\n'
 receipts.append(dict(entry=entry,source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),bytecode_sha256=hashlib.sha256(b).hexdigest()))
(root/'OptiScaler/shaders/format_transfer/precompile/PresentColor_Shader.h').write_text(header)
if '--composition' in __import__('sys').argv:
 src=root/'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl';dst=src.parent/'DlssNr_Shader.cso'
 subprocess.run([str(fxc),'/nologo','/T','cs_5_0','/E','CSMain','/Ges','/WX','/O3','/Fo',str(dst),str(src)],check=True)
 b=dst.read_bytes();(src.parent/'DlssNr_Shader.h').write_text('#pragma once\ninline static const unsigned char DlssNr_cso[] = {\n'+'\n'.join('    '+','.join(f'0x{x:02x}' for x in b[i:i+16])+',' for i in range(0,len(b),16))+'\n};\n')
 receipts.append(dict(entry='CSMain',source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),bytecode_sha256=hashlib.sha256(b).hexdigest()))
(out/'shader-compilation.json').write_text(json.dumps(dict(compiler=str(fxc),compiler_sha256=hashlib.sha256(fxc.read_bytes()).hexdigest(),shaders=receipts),indent=2))
