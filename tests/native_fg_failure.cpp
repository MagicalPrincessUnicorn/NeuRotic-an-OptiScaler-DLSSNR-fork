#include "../addons/native-framegen/SdkDispatch.h"
#include <cassert>
#include <cstdio>
namespace NrFgSdk {void (*enter)(bool)=nullptr;void (*leave)(bool)=nullptr;}
static unsigned calls=0;
static VkResult Fail(){++calls;return VK_ERROR_DEVICE_LOST;}
static VkResult Okay(){++calls;return VK_SUCCESS;}
int main(){
 assert(NrFgSdk::Invoke(true,Fail)==VK_ERROR_DEVICE_LOST&&calls==1);
 assert(NrFgSdk::Invoke(true,Okay)==VK_ERROR_DEVICE_LOST&&calls==1);
 assert(NrFgSdk::Invoke(false,Okay)==VK_ERROR_DEVICE_LOST&&calls==1);
 puts("PASS: failed SDK submission prevents later timeline work");
}
