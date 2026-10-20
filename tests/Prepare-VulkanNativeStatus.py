"""Prepare bounded CPU fixtures from actual production bodies. Does not compile/run."""
from pathlib import Path
import argparse,hashlib,json

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--output-directory',type=Path,required=True)
    args=parser.parse_args();root=args.source_root.resolve();out=args.output_directory.resolve()
    if out==root or out==root/'tests' or out==root/'OptiScaler':
        raise ValueError('Use a dedicated generated fixture directory')
    pins={}
    def read(relative):
        p=root/relative;raw=p.read_bytes();pins[str(p)]=hashlib.sha256(raw).hexdigest()
        return raw.decode('utf-8-sig').replace('\r\n','\n')
    session=read('OptiScaler/dlssnr/VulkanNrSession.cpp')
    record=session[session.index('VkRecordResult VulkanNrSession::Record('):session.index('void VulkanNrSession::PollCompletions()')]
    runtime=read('OptiScaler/dlssnr/VulkanNrRuntime.cpp');runtime=runtime[runtime.index('namespace DlssNr'):]
    feature=read('OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
    terminal=feature[feature.index('void ObserveNativeResultVk('):feature.index('static std::optional<double> NativeGpuTimeVkLocked();')]
    if record.count('VulkanNrSession::Record(')!=1 or terminal.count('NrConfigSnapshot<Config>')!=1:
        raise ValueError('Unexpected actual source projection boundary')
    generation=read('OptiScaler/dlssnr/VulkanNrGeneration.cpp')
    generation=generation[generation.index('namespace DlssNr'):generation.index('VkNrGenerationOwner::VkNrGenerationOwner')]+ '\n}\n'
    status=read('tests/vulkan_native_status.cpp')
    if status.count(generation)!=1:
        raise ValueError('Embedded generation compatibility projection is stale; update the fixture from actual source')
    caller=read('OptiScaler/inputs/NVNGX_DLSS_Vk.cpp')
    a=caller.index('    std::optional<DlssNr::VkNrNativeObservation> nrObservation;')
    capture=caller[a:caller.index('    std::shared_ptr<const DlssNr::VkNrColorIngress>',a)]
    blocks=[('vulkan_native_status.cpp',status,{}),
        ('vulkan_native_epoch.cpp',read('tests/vulkan_native_epoch.cpp.in'),{
            '// ACTUAL_SESSION_RECORD\n':record.replace('VulkanNrSession::Record(','EpochSession::Record('),
            '// ACTUAL_RUNTIME\n':runtime}),
        ('vulkan_native_input_epoch.cpp',read('tests/vulkan_native_input_epoch.cpp.in'),{
            '// ACTUAL_NATIVE_TERMINAL\n':terminal.replace('NrConfigSnapshot<Config>','NrConfigSnapshot<TestConfig>')}),
        ('vulkan_native_observation_device.cpp',read('tests/vulkan_native_observation_device.cpp.in'),{
            '// ACTUAL_NATIVE_CAPTURE\n':capture})]
    out.mkdir(parents=True,exist_ok=True);generated=[]
    for name,text,replacements in blocks:
        for marker,body in replacements.items():
            if text.count(marker)!=1:raise ValueError('Missing or repeated actual source marker: '+marker)
            text=text.replace(marker,body)
        if '// ACTUAL_' in text:raise ValueError('Unexpanded source marker')
        p=out/name;p.write_bytes(text.replace('\n','\r\n').encode())
        generated.append({'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    report={'status':'PREPARED_NOT_COMPILED_OR_EXECUTED','source_pins':pins,'generated':generated,
        'projection':'Complete actual Session::Record (class owner rename only), Runtime EnsureDevice/Activate unchanged, terminal ObserveNativeResultVk Config template source becomes TestConfig. Main production inline headers unchanged; embedded full generation compatibility bodies validated against current production. Epoch fixture deliberately has NULLcommandBuffer/zerouse and failclosed unreachable recording/completion stubs. No Vulkan/provider/model/GPU operations or execution rights are established.'}
    p=out/'PREPARATION.json';p.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'preparation':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'generated':generated}))

if __name__=='__main__':main()
