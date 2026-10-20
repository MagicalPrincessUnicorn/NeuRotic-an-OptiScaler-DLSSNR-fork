"""Losslessly embed native display regions from the official clean identity PNG."""
from pathlib import Path
from PIL import Image
import hashlib,json
root=Path(__file__).resolve().parents[2];B=root/'apps/NeuRoticHub/ui/brand'
source=B/'NeuRotic-startup-reference.png';digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert digest(source)=='f0fb869157fbb921cf9bd4b7b513a80e5ef48353f0fa163209040ea8507a4be7'
im=Image.open(source).convert('RGBA');assert im.size==(1536,1024)
regions={'Mark':(568,172,955,686),'Wordmark':(383,697,1151,795)}
lines=['// Generated lossless pixel data from the official clean NeuRotic identity.',
'// Source: NeuRotic_Everything_Pack/concepts_and_boards/02_approved_clean_logo_with_wordmark.png',
'// Whole original PNG SHA-256: '+digest(source),
'// Native display regions only; source pixels, gradient and letterforms unchanged.']
facts=[]
for name,(x0,y0,x1,y1) in regions.items():
 # Select exact native display source rows, preserving every RGBA byte.
 all_pixels=im.tobytes();raw=b''.join(all_pixels[(y*im.width+x0)*4:(y*im.width+x1)*4] for y in range(y0,y1));assert set(raw[3::4])=={255}
 pixels=[raw[i:i+4] for i in range(0,len(raw),4)];packed=bytearray();i=0
 def run(index):
  end=index+1
  while end<len(pixels) and end-index<32767 and pixels[end]==pixels[index]:end+=1
  return end-index
 while i<len(pixels):
  count=run(i)
  if count>=3:packed+=(count|0x8000).to_bytes(2,'little')+pixels[i];i+=count
  else:
   start=i;i+=count
   while i<len(pixels) and i-start<32767 and run(i)<3:i+=1
   count=i-start;packed+=count.to_bytes(2,'little')+raw[start*4:i*4]
 decoded=bytearray();offset=0
 while offset<len(packed):
  tag=int.from_bytes(packed[offset:offset+2],'little');offset+=2;count=tag&0x7fff
  if tag&0x8000:decoded+=packed[offset:offset+4]*count;offset+=4
  else:decoded+=packed[offset:offset+count*4];offset+=count*4
 assert decoded==raw
 lines.extend([f'inline constexpr int {name}Width={x1-x0}, {name}Height={y1-y0};',f'inline constexpr unsigned char {name}Packed[]={{'])
 for start in range(0,len(packed),24):lines.append(' '+','.join(f'0x{b:02x}' for b in packed[start:start+24])+',')
 lines.append('};')
 facts.append({'name':name,'source_rectangle':[x0,y0,x1,y1],'dimensions':[x1-x0,y1-y0],'source_rgba_sha256':hashlib.sha256(raw).hexdigest(),'pixel_bytes':len(raw),'packed_bytes':len(packed),'round_trip':'ALL_RGBA_BYTES_EQUAL','alpha':'all255; nativeDIB32opaque source'} )
p=B/'StartupBrandPixels.inc';p.write_text('\n'.join(lines)+'\n',encoding='ascii')
print(json.dumps({'source_sha256':digest(source),'output_sha256':digest(p),'regions':facts}))
