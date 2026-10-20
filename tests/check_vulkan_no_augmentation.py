"""Verify the production startup modifiers preserve all caller data in this diagnostic."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'builds/vulkan-experiment/tests/vulkan-only'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'OptiScaler/spoofing/Vulkan_Spoofing.cpp').read_text(encoding='utf-8-sig')
functions = []
for name in ['hkvkCreateInstance', 'hkvkCreateDevice']:
    start = source.index('VkResult VulkanSpoofing::' + name + '(')
    end = source.index('\n}\n', start) + 3
    functions.append(source[start:end])
fixture = '''#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring>
class VulkanSpoofing { public:
static VkResult hkvkCreateInstance(VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
static VkResult hkvkCreateDevice(VkPhysicalDevice,VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
};
''' + '\n'.join(functions) + '''
int main() {
const char* extensions[]={"test_extension"};
VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_3;
VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
instance.pApplicationInfo=&app;instance.enabledExtensionCount=1;instance.ppEnabledExtensionNames=extensions;
auto savedInstance=instance;auto savedApp=app;
VkInstance outputInstance=reinterpret_cast<VkInstance>(uintptr_t(17));
assert(VulkanSpoofing::hkvkCreateInstance(&instance,nullptr,&outputInstance)==VK_SUCCESS);
assert(std::memcmp(&instance,&savedInstance,sizeof(instance))==0);
assert(std::memcmp(&app,&savedApp,sizeof(app))==0);
assert(outputInstance==reinterpret_cast<VkInstance>(uintptr_t(17)));
VkPhysicalDeviceFeatures features{};features.robustBufferAccess=VK_TRUE;auto savedFeatures=features;
VkPhysicalDeviceTimelineSemaphoreFeatures chain{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
chain.timelineSemaphore=VK_TRUE;auto savedChain=chain;
VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device.pNext=&chain;
device.pEnabledFeatures=&features;device.enabledExtensionCount=1;device.ppEnabledExtensionNames=extensions;
auto savedDevice=device;VkDevice outputDevice=reinterpret_cast<VkDevice>(uintptr_t(23));
assert(VulkanSpoofing::hkvkCreateDevice(nullptr,&device,nullptr,&outputDevice)==VK_SUCCESS);
assert(std::memcmp(&device,&savedDevice,sizeof(device))==0);
assert(std::memcmp(&features,&savedFeatures,sizeof(features))==0);
assert(std::memcmp(&chain,&savedChain,sizeof(chain))==0);
assert(outputDevice==reinterpret_cast<VkDevice>(uintptr_t(23)));
assert(VulkanSpoofing::hkvkCreateInstance(nullptr,nullptr,nullptr)==VK_ERROR_INITIALIZATION_FAILED);
assert(VulkanSpoofing::hkvkCreateDevice(nullptr,nullptr,nullptr,nullptr)==VK_ERROR_INITIALIZATION_FAILED);
}
'''
(out / 'creation.cpp').write_text(fixture, encoding='utf-8')
vcvars = r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
prefix = '@echo off\ncall "' + vcvars + '" -vcvars_ver=14.44 >nul\n'
flags = '/nologo /DNR_DIAG_VULKAN_NO_AUGMENT=1 /I"' + str(root / 'external/vulkan/include') + '" '
(out / 'creation-preprocess.cmd').write_text(prefix + 'cl ' + flags + '/EP creation.cpp > creation.i\nexit /b %errorlevel%\n')
result = subprocess.run(['cmd','/d','/c','creation-preprocess.cmd'],cwd=out,capture_output=True,text=True)
assert result.returncode == 0, result.stdout + result.stderr
processed = (out / 'creation.i').read_text()
body = processed[processed.index('VkResult VulkanSpoofing::'):processed.index('int main()')]
if 'newExtensionList' in body or 'IdentifyGpu::' in body:
    sys.exit('FAIL: diagnostic still executes generic Vulkan startup augmentation')
(out / 'creation-compile.cmd').write_text(prefix + 'cl ' + flags + '/EHsc /O2 creation.cpp /Fe:creation.exe /Fo:creation.obj /link /INCREMENTAL:NO\nif errorlevel 1 exit /b %errorlevel%\ncreation.exe\nexit /b %errorlevel%\n')
result = subprocess.run(['cmd','/d','/c','creation-compile.cmd'],cwd=out,capture_output=True,text=True)
print(result.stdout + result.stderr)
if result.returncode == 0: print('PASS: production modifiers preserve caller creation structures, chains, features and outputs')
sys.exit(result.returncode)
