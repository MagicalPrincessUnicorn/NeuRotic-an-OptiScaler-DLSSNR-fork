"""Extract current Native temporal scalar acquisition/validation for host-only testing."""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
source=ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp';text=source.read_text(encoding='utf-8-sig')
old='    if (params->Get(NVSDK_NGX_Parameter_MV_Scale_X, &frame.MvScaleX) != NVSDK_NGX_Result_Success)'
new='    const auto scalarFacts = NativeTemporalInputFacts::Resolve('
start=new if new in text else old
assert text.count(start)==1
begin=text.index(start)
end_anchor='    auto inputProvenance =' if new in text else '    if (forceAfterUpscale && target == g_nr.preSrScratch)'
end=text.index(end_anchor,begin)
body=text[begin:end]
template=Path(__file__).with_suffix('.cpp.in');facts=ROOT/'OptiScaler/dlssnr/NativeTemporalInputFacts.h'
output=template.read_text().replace('@SCALARS@',body).replace('@FACTS@',facts.as_posix())
# The production function is inside DlssNr; retain its lookup context in the standalone fixture.
output=output.replace('unsigned continued=0', '#ifdef HAS_TEMPORAL_FACTS\nnamespace NativeTemporalInputFacts=DlssNr::NativeTemporalInputFacts;\n#endif\nunsigned continued=0')
args.output.write_text(output,encoding='utf-8');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt={'renderer_sha256':sha(source),'generator_sha256':sha(Path(__file__)),'template_sha256':sha(template),
'generated_sha256':sha(args.output),'extracted_block_sha256':hashlib.sha256(body.encode()).hexdigest(),
'source_start_line':text[:begin].count('\n')+1,'scope':'Host-only exact scalar block and parameter endpoint double; no full renderer/provider/GPU qualification'}
args.output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
