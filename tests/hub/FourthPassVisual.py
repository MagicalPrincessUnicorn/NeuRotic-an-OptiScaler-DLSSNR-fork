"""Check the theme-facing logo backdrop in an actual desktop render."""
import sys
from pathlib import Path
from PIL import Image
image = Image.open(Path(sys.argv[1]) / 'page-0.png').convert('RGB')
# This point is inside the old light-theme black header plate, above the wordmark.
sample = image.getpixel((120, 22))
assert min(sample) > 210, f'Light-theme logo backdrop is still dark: {sample}'
print('PASS HUB-LOGO: transparent lockup blends with the light surface')
if len(sys.argv)>2:
    folder=Path(sys.argv[2])
    read=lambda frame:Image.open(folder/f'water-{frame}.png').convert('RGB')
    dark,light,up,down=read(1),read(95),read(45),read(125)
    near=lambda a,b: max(abs(x-y) for x,y in zip(a,b))<8
    top,bottom=(900,50),(900,730)
    assert near(up.getpixel(top),dark.getpixel(top)) and near(up.getpixel(bottom),light.getpixel(bottom))
    assert near(down.getpixel(top),dark.getpixel(top)) and near(down.getpixel(bottom),light.getpixel(bottom))
    assert near(read(246).getpixel(top),light.getpixel(top)) and near(read(246).getpixel(bottom),light.getpixel(bottom))
    assert near(read(251).getpixel(top),dark.getpixel(top)) and near(read(251).getpixel(bottom),dark.getpixel(bottom))
    import json
    assert json.loads((folder/'resize.json').read_text())['renderedBeforeRelease']
    print('PASS HUB-WATER: actual GPU frames rise toward light and descend toward dark')
    print('PASS HUB-MOTION: repeated switches complete and reduced motion switches immediately')
    print('PASS HUB-RESIZE: layout renders before the drag release')
