from pathlib import Path
import argparse, hashlib, json, datetime, re

parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parent.parent)
parser.add_argument('--output-directory',type=Path,required=True)
args=parser.parse_args()
R=args.source_root.resolve();B=args.output_directory.resolve()
if B in [R,R/'tests',R/'OptiScaler']:raise ValueError('Use a dedicated generated fixture directory')
B.mkdir(parents=True,exist_ok=True)
pins={}
def read(relative):
    p=R/relative;pins[relative]=hashlib.sha256(p.read_bytes()).hexdigest()
    return p.read_text(encoding='utf-8-sig')
feature=read('OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
caller=read('OptiScaler/inputs/NVNGX_DLSS_Vk.cpp')
menu=read('OptiScaler/dlssnr/DlssNr_Menu.cpp')
status=read('OptiScaler/dlssnr/DlssNr_MenuStatus.h')
for relative in ['OptiScaler/inputs/VulkanNativeFeatures.h','OptiScaler/dlssnr/VulkanNrPreSr.h','OptiScaler/dlssnr/VulkanNrNativeObservation.h']:
    read(relative)
terminal=feature[feature.index('void ObserveNativeResultVk('):feature.index('static std::optional<double> NativeGpuTimeVkLocked();')]
destroy=feature[feature.index('void DeviceDestroyedVk('):feature.index('void NotifyDeviceInitVk()')]
a=caller.index('    std::optional<DlssNr::VkNrNativeObservation> nrObservation;')
capture=caller[a:caller.index('    std::shared_ptr<const DlssNr::VkNrColorIngress>',a)]
a=caller.index('uint64_t VulkanNativeFeatures::BeginDeviceRetirement(')
bridge=caller[a:caller.index('static void RegisterNativeFeature(',a)]
hook=read('OptiScaler/hooks/Vulkan_Hooks.cpp')
a=hook.index('static void VKAPI_CALL nrDestroyDevice(')
hook=hook[a:hook.index('static VkResult VKAPI_CALL nrCreateImage(',a)]
a=menu.index('MenuReadiness MenuPreflightState(')
a=menu.index('    const auto telemetry = SelectedNativeTelemetry();',menu.index('    return ready ? MenuReadiness::Ready : MenuReadiness::Waiting;',a))
tail=menu[a:menu.index('// Nested panels',a)].rstrip()
activity=status[status.index('struct ActivityObservation'):status.index('// Presentation only:')]
a=re.search(r'static void ShutdownVkLocked\([^\n]+\)\n\{',feature).start()
prelude=feature[a:feature.index('    ClearFinalSource(ActiveVk());',a)]
prelude=prelude.replace('bool deviceAlive','[[maybe_unused]] bool deviceAlive')
prelude+='    // CPU fixture adapter: resource teardown is NOT executed or qualified.\n    ++resourceCleanupCalls;ActiveVk().device=VK_NULL_HANDLE;\n}\n'
a=feature.index('static void ShutdownVkSessionsLocked(')
sessions=feature[a:feature.index('void ShutdownVk(bool deviceAlive)',a)]
replacements={'// ACTUAL_NATIVE_TERMINAL\n':terminal.replace('NrConfigSnapshot<Config>','NrConfigSnapshot<TestConfig>'),
              '// ACTUAL_DEVICE_DESTROYED\n':destroy,'// ACTUAL_CAPTURE\n':capture,
              '// ACTUAL_DEVICE_BRIDGE\n':bridge,'// ACTUAL_DESTROY_HOOK\n':hook,'// ACTUAL_SHUTDOWN_PRELUDE\n':prelude,'// ACTUAL_SHUTDOWN_SESSIONS\n':sessions,'// ACTUAL_MENU_TAIL\n':tail+'\n',
              '// ACTUAL_ACTIVITY\n':activity}
generated=[]
for name in ['vulkan_review_lifetime.cpp','vulkan_review_restore.cpp','vulkan_review_preflight.cpp','vulkan_review_retirement.cpp','vulkan_review_nested_retirement.cpp']:
    template=R/'tests'/(name+'.in')
    text=template.read_text();pins[str(template)]=hashlib.sha256(template.read_bytes()).hexdigest()
    for marker,body in replacements.items():
        if marker in text:
            assert text.count(marker)==1
            text=text.replace(marker,body)
    assert '// ACTUAL_' not in text
    p=B/name;p.write_bytes(text.replace('\n','\r\n').encode())
    generated.append({'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
d={'status':'PREPARED_NOT_EXECUTED','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
   'source_pins':pins,'generated':generated,
   'projection':'Nested fixture includes complete actual shutdown route/control loop and CPU prelude through lastGpuFrame reset; remainder is explicit counted CPU cleanup adapter, not resource execution or qualification. Complete actual capture, terminal, device destruction and registry bridge; only Config template becomes TestConfig. Actual native menu branch and full ActivityObservation unchanged. Actual pre-SR scope header and real recording owner CPU bookkeeping. Fake Present executor has no-op device callback; model shutdown stub aborts if reached; all ten pass slots have no model/device. No Vulkan API/provider/model/game/GPU execution.',
   'regression_contract':'Lifetime: dead source selected diagnostics persist and late capture remains current/publishes; restore: untouched missing jitter incorrectly false; menu: disabled raw progress never ready. Predictions only. Existing true mutation/failed rollback, enabled selected/raw separation, timeout/reuse/other-device and DX12 controls required.',
   'ownership':'03 sole source writer;05 compiler/native/cache. Prior Source05 and FG leases released. Existing human Vulkan same-review correction grant call_e4fb034ee50149239d0d762b39dee254; no new runtime authority.'}
p=B/'SOURCE-03.json';p.write_text(json.dumps(d,indent=2)+'\n')
print(json.dumps({'manifest':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'generated':generated}))
