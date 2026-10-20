from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
out=root/'docs/implementation-evidence/menu-water-20261003'
out.mkdir(parents=True,exist_ok=True)
frames=root/'builds/ui-release/sleek/water-frames'
numbers=[261,285,300,324,480,504,519,543,585,600,615,645]
canvas=Image.new('RGB',(1800,4*510),(18,22,30))
for i,n in enumerate(numbers):
    with Image.open(frames/f'frame-{n:03}.bmp') as im:
        im.convert('RGB').save(out/f'frame-{n:03}.png')
        canvas.paste(im.convert('RGB').resize((600,480),Image.Resampling.LANCZOS),((i%3)*600,(i//3)*510+30))
    ImageDraw.Draw(canvas).text(((i%3)*600+10,(i//3)*510+8),f'Frame {n}',fill='white')
canvas.save(out/'water-contact.png')
images=[]
for n in range(480,720,3):
    with Image.open(frames/f'frame-{n:03}.bmp') as im: images.append(im.convert('RGB').resize((750,600),Image.Resampling.LANCZOS))
images[0].save(out/'water-down-reversal.gif',save_all=True,append_images=images[1:],duration=50,loop=0,optimize=True)
# Only these owned, regenerable preview BMPs; keep compressed visual evidence.
size=0
for folder in [frames,root/'builds/ui-release/sleek/frames']:
    for file in folder.glob('frame-*.bmp'):
        size+=file.stat().st_size;file.unlink()
print(f'Preserved PNG/GIF visual evidence in {out}; removed {size} bytes of regenerable BMPs')
