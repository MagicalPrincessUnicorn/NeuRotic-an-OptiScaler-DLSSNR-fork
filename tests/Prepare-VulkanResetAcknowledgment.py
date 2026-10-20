from pathlib import Path
import hashlib,json,re,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
rel='OptiScaler/dlssnr/DlssNrFeature_Vk.cpp';base='8890254ed702a65876ff48266e6184d8e2120846'
old=subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root)
current=(root/rel).read_bytes()
pins=[]
for label,raw,terminal in [('old',old,'if (ActiveVk().resetRevision == resetRevision) ActiveVk().reset = false;'),('current',current,'ActiveVk().resetState.Acknowledge(resetRevision);')]:
    text=raw.decode('utf-8');model=text.index('const VkFrame::Inputs frame');start=text.index('    if (!prepared)',model)
    end=text.index(terminal,start)+len(terminal)
    body=text[start:end]
    # Timestamp is a GPU diagnostic outside this CPU acknowledgment control.
    body,removed=re.subn(r'    if\(timing\)vkCmdWriteTimestamp\([^;]+;\s*\n','',body)
    assert removed==1
    body=body.replace('ActiveVk()','state')
    assert not re.search(r'vkCmd|ActiveVk\(\)',body)
    frame=text[model:text.index('int evaluated',model)]
    arguments=re.findall(r'depthInverted,\s*(ActiveVk\(\)\.(?:resetState\.Pending\(\)|reset)),',frame)
    assert len(arguments)==1
    argument=arguments[0].replace('ActiveVk()','state')
    target=out/(label+'_model_ack.inc');target.write_text(body+'\n',encoding='utf-8')
    input_path=out/(label+'_model_reset_input.inc');input_path.write_text('return '+argument+';\n',encoding='utf-8')
    pins.append(dict(label=label,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),reset_input_sha256=hashlib.sha256(input_path.read_bytes()).hexdigest(),timestamp_statements_omitted=removed))
print(json.dumps(dict(baseline=base,path=rel,pins=pins,adaptation='ActiveVk identifier bound to authored state; LOG_ERROR discarded; Fail records refusal only. Actual three model guards, reset-input expression, recording marker and acknowledgment statement retained. No GPU/private callback execution.')))
