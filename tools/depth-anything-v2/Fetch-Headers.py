"""Developer headers only; no machine-wide SDK installation or runtime payloads."""
import hashlib, io, json, pathlib, re, tarfile, urllib.request, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[2] / '_deps' / 'depth-anything-v2'
ROOT.mkdir(parents=True, exist_ok=True)
def download(url):
    with urllib.request.urlopen(url, timeout=60) as response: return response.read()
records = []
def header(name):
    path = ROOT / 'tensorrt-10.13' / 'include' / name
    if path.exists(): return
    data = download('https://raw.githubusercontent.com/NVIDIA/TensorRT/release/10.13/include/' + name)
    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    records.append({'name': name, 'sha256': hashlib.sha256(data).hexdigest()})
    for dependency in re.findall(rb'#include "(Nv[^"/]+\.h)"', data): header(dependency.decode())
header('NvInfer.h')
parser = ROOT / 'tensorrt-10.13/include/NvOnnxParser.h'
if not parser.exists(): parser.write_bytes(download('https://raw.githubusercontent.com/onnx/onnx-tensorrt/10.13-GA/NvOnnxParser.h'))
url = 'https://conda.anaconda.org/nvidia/noarch/cuda-cudart-dev_win-64-12.9.79-0.conda'
path = ROOT / 'cuda-cudart-dev_win-64-12.9.79-0.conda'
if not path.exists(): path.write_bytes(download(url))
data = path.read_bytes()
assert hashlib.sha256(data).hexdigest() == '2eebee9bd5ea50f29ef57c30a449d75bd417df89e570dad53bca9df0768a63b0'
with zipfile.ZipFile(io.BytesIO(data)) as archive:
    payload = next(n for n in archive.namelist() if n.startswith('pkg-'))
    # zstandard is an optional developer extraction dependency, never a product dependency.
    import zstandard
    raw = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(archive.read(payload)))
    with tarfile.open(fileobj=raw, mode='r|') as tar:
        tar.extractall(ROOT / 'cuda-12.9', filter='data')
records.append({'url': url, 'sha256': hashlib.sha256(data).hexdigest()})
url = 'https://conda.anaconda.org/nvidia/noarch/cuda-crt-dev_win-64-12.9.86-0.conda'
path = ROOT / 'cuda-crt-dev_win-64-12.9.86-0.conda'
if not path.exists(): path.write_bytes(download(url))
data = path.read_bytes()
assert hashlib.sha256(data).hexdigest() == 'a6f6fca1b582e7c8c4a9f56f5c5f3293991bf08da493c7c6cdaa527bc6899383'
with zipfile.ZipFile(io.BytesIO(data)) as archive:
    payload = next(n for n in archive.namelist() if n.startswith('pkg-'))
    raw = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(archive.read(payload)))
    with tarfile.open(fileobj=raw, mode='r|') as tar:
        for member in tar:
            if member.isfile() and member.name.startswith('Library/include/'):
                tar.extract(member, ROOT / 'cuda-12.9', filter='data')
records.append({'url': url, 'sha256': hashlib.sha256(data).hexdigest()})
records.extend({'name': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in (ROOT / 'tensorrt-10.13/include').glob('*.h'))
(ROOT / 'headers-receipt.json').write_text(json.dumps(records, indent=2))
print('Developer headers extracted to', ROOT)

