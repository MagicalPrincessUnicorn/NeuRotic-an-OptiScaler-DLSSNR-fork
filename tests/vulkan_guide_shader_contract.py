"""Verify sampled guide shader storage formats and integer, unfiltered image fetch."""
from pathlib import Path
import struct
root=Path(__file__).resolve().parents[1]
for name,fmt in [('GuideDepthCopy_spv',3),('GuideMotionCopy_spv',1)]:
 data=(root/'builds/vulkan-experiment/shaders'/f'{name}.spv').read_bytes()
 words=struct.unpack('<'+'I'*(len(data)//4),data)
 assert words[0]==0x07230203
 instructions=[]
 i=5
 while i<len(words):
  count=words[i]>>16;op=words[i]&65535
  assert count
  instructions.append((op,words[i+1:i+count]));i+=count
 # OpTypeImage: sampled input and a typed float32 storage output, no optional extended formats.
 assert any(op==25 and args[6]==2 and args[7]==fmt for op,args in instructions),name
 assert any(op==95 for op,_ in instructions),'must use OpImageFetch'
 assert any(op==99 for op,_ in instructions),'must store private image'
 assert not any(op in (86,87,88,89,90,91,92,93,94,96,97) for op,_ in instructions),'no filtered sampling'
 assert not any(op==59 and args[2]==9 for op,args in instructions),'no push constants'
 assert not any(op==17 and args[0] in (49,55,56) for op,args in instructions),'no extended/unformatted storage capability'
print('PASS depth R32F / motion RGBA32F SPIR-V exact fetch/store; no filtering/push constants/optional formats')
