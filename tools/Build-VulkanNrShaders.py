"""Compile/verify Vulkan NR SPIR-V and exact compiler/source/embedded identities."""
from pathlib import Path
import subprocess,hashlib,json,sys,re,struct
root=Path(__file__).resolve().parents[1]
out=root/'builds/vulkan-experiment/shaders';out.mkdir(parents=True,exist_ok=True)
dxc=root/'OptiScaler/shaders/shader_tools/dxc.exe'
receipt=root/'docs/implementation-evidence/vulkan-experiment/VULKAN-SHADERS.json'
entries=[('format_transfer','PresentColor','Decode','PresentColorDecode_spv'),('format_transfer','PresentColor','Encode','PresentColorEncode_spv'),('dlssnr','dlssnr','CSMain','dlssnr_spv'),('dlssnr','GuideCopy','CopyDepth','GuideDepthCopy_spv'),('dlssnr','GuideCopy','CopyMotion','GuideMotionCopy_spv')]
def sha(data):return hashlib.sha256(data).hexdigest()
headers={};rows=[]
for folder,name,entry,symbol in entries:
 src=root/f'OptiScaler/shaders/{folder}/precompile/{name}.hlsl';dst=out/f'{symbol}.spv'
 args=['-spirv','-fspv-target-env=vulkan1.1','-T','cs_6_0','-E',entry,'-D','VK_MODE','-O3','-Ges','-WX']
 subprocess.run([str(dxc),*args,'-Fo',str(dst),str(src)],check=True)
 data=dst.read_bytes()
 if symbol=='dlssnr_spv':
  words=struct.unpack('<'+'I'*(len(data)//4),data);instructions=[];at=5
  while at<len(words):
   count=words[at]>>16;op=words[at]&65535
   if not count:raise SystemExit('FAIL malformed composition SPIR-V')
   instructions.append((op,words[at+1:at+count]));at+=count
  images=[a for op,a in instructions if op==25 and a[6]==2]
  if not images or any(a[7]!=0 for a in images):raise SystemExit('FAIL composition requires Unknown storage formats for mixed targets')
  if not any(op==17 and a[0]==56 for op,a in instructions):raise SystemExit('FAIL missing StorageImageWriteWithoutFormat capability')
  if any(op==98 or (op==17 and a[0]==55) for op,a in instructions):raise SystemExit('FAIL composition acquired an unprepared storage-read feature requirement')
 header=root/f'OptiScaler/shaders/{folder}/precompile/{"GuideCopy_Shader_Vk" if name=="GuideCopy" else "PresentColor_Shader_Vk" if folder=="format_transfer" else "DlssNr_Shader_Vk"}.h'
 headers.setdefault(header,'#pragma once\n')
 headers[header]+='inline static const unsigned char '+symbol+'[] = {\n'+'\n'.join('    '+','.join(f'0x{x:02x}' for x in data[i:i+16])+',' for i in range(0,len(data),16))+'\n};\n'
 # Byte identity is checked independently of whitespace in the embedded header.
 rows.append(dict(source=str(src.relative_to(root)).replace('\\','/'),entry=entry,symbol=symbol,args=args,source_sha256=sha(src.read_bytes()),bytecode_sha256=sha(data)))
result=dict(compiler=str(dxc.relative_to(root)).replace('\\','/'),compiler_sha256=sha(dxc.read_bytes()),shaders=rows)
if '--verify' in sys.argv:
 if json.loads(receipt.read_text(encoding='utf-8'))!=result:raise SystemExit('FAIL shader receipt differs from fresh compilation')
 for header in headers:
  existing=header.read_text(encoding='utf-8')
  for row in [r for r in rows if r['symbol'] in headers[header]]:
   body=re.search(r'\b'+row['symbol']+r'\[\]\s*=\s*\{(.*?)\};',existing,re.S).group(1)
   data=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',body))
   if sha(data)!=row['bytecode_sha256']:raise SystemExit('FAIL embedded shader differs: '+row['symbol'])
 print('PASS fresh compiler/source/bytecode/embedded shader identities')
else:
 for header,text in headers.items():header.write_text(text,encoding='utf-8')
 receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print('PASS Vulkan Decode/Encode/composition shader compilation and receipts')
