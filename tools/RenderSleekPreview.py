"""Turn the offline DX11 component frames into reviewable PNGs and an animated GIF."""
from pathlib import Path
from PIL import Image
import sys
root=Path(__file__).resolve().parents[1]
frames=root/'builds/ui-release/sleek/frames'
output=root/'docs/implementation-evidence/ui-reveal-spacing-20261002/preview'
output.mkdir(parents=True,exist_ok=True)
paths=sorted(frames.glob('frame-*.bmp'))
if len(paths)!=160:
    raise RuntimeError(f'Expected 160 rendered frames, received {len(paths)}')
images=[]
for path in paths:
    with Image.open(path) as image:
        images.append(image.convert('RGB').resize((1000,800),Image.Resampling.LANCZOS))
for source,name in [('frame-135.bmp','dark.png'),('frame-417.bmp','light.png'),('frame-045.bmp','waiting.png'),('frame-435.bmp','heart.png')]:
    with Image.open(frames/source) as image: image.save(output/name)
images[0].save(output/'motion.gif',save_all=True,append_images=images[1:],duration=50,loop=0,optimize=True)
print(f'Created {output} ({len(images)} frames, 8 seconds)')
# These BMPs are regenerable staging output belonging only to this preview.
for path in paths: path.unlink()
