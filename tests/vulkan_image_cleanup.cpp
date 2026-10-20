#include "../OptiScaler/dlssnr/VulkanNrImageFacts.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
using namespace DlssNr;
static void Check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
template<class T> T Handle(uintptr_t n){return reinterpret_cast<T>(n);}
static VkImageCreateInfo ImageInfo(){
    VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;
    c.format=VK_FORMAT_R16G16B16A16_SFLOAT;c.extent={32,32,1};c.mipLevels=c.arrayLayers=1;
    c.samples=VK_SAMPLE_COUNT_1_BIT;c.usage=VK_IMAGE_USAGE_SAMPLED_BIT;return c;
}
static VkImageViewCreateInfo ViewInfo(VkImage image){
    VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};c.image=image;
    c.viewType=VK_IMAGE_VIEW_TYPE_2D;c.format=ImageInfo().format;c.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return c;
}
static NVSDK_NGX_Resource_VK Resource(VkImage image,VkImageView view){
    NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    auto& v=r.Resource.ImageViewInfo;v.Image=image;v.ImageView=view;v.Format=ImageInfo().format;
    v.Width=v.Height=32;v.SubresourceRange=ViewInfo(image).subresourceRange;
    return r;
}
static bool Allowed(VkNrImageFacts& facts,VkDevice device,VkImage image,VkImageView view){
    return facts.Rights(device,Resource(image,view),VK_IMAGE_USAGE_SAMPLED_BIT,false).has_value();
}
static void Lifetime(){
    VkNrImageFacts facts;auto d=Handle<VkDevice>(1),other=Handle<VkDevice>(2);
    auto a=Handle<VkImage>(100),b=Handle<VkImage>(200),c=Handle<VkImage>(300);
    auto x=Handle<VkImageView>(1000),y=Handle<VkImageView>(1001),z=Handle<VkImageView>(1002);
    for(auto i:{a,b})facts.Created(d,ImageInfo(),i,VK_SUCCESS);
    facts.Created(other,ImageInfo(),c,VK_SUCCESS);
    facts.ViewCreated(d,ViewInfo(a),x,VK_SUCCESS);facts.ViewCreated(d,ViewInfo(a),y,VK_SUCCESS);
    facts.ViewCreated(other,ViewInfo(c),z,VK_SUCCESS);
    Check(Allowed(facts,d,a,x)&&Allowed(facts,d,a,y),"multiple views admitted");
    facts.Destroyed(other,a);Check(Allowed(facts,d,a,x),"wrong-device destroy ignored");
    facts.ViewCreated(d,ViewInfo(b),x,VK_ERROR_OUT_OF_DEVICE_MEMORY);
    Check(Allowed(facts,d,a,x),"failed view creation leaves previous facts");
    // Simulate handle reuse/duplicate observation: replacing a view must detach its old index.
    facts.ViewCreated(d,ViewInfo(b),x,VK_SUCCESS);facts.Destroyed(d,a);
    Check(Allowed(facts,d,b,x)&&!Allowed(facts,d,a,y),"image erase keeps reassigned view and invalidates own views");
    facts.Created(d,ImageInfo(),a,VK_SUCCESS);Check(!Allowed(facts,d,a,y),"reused image cannot revive old view");
    facts.ViewCreated(d,ViewInfo(a),y,VK_SUCCESS);
    facts.Created(d,ImageInfo(),a,VK_SUCCESS);Check(!Allowed(facts,d,a,y),"new image generation rejects previous view");
    facts.ViewDestroyed(other,x);Check(Allowed(facts,d,b,x),"wrong-device view erase ignored");
    facts.ViewDestroyed(d,x);facts.ViewDestroyed(d,x);Check(!Allowed(facts,d,b,x),"view erase idempotent");
    facts.ViewCreated(d,ViewInfo(b),x,VK_SUCCESS);Check(Allowed(facts,d,b,x),"view handle reusable");
    facts.DeviceDestroyed(d);Check(!Allowed(facts,d,b,x)&&Allowed(facts,other,c,z),"device erase preserves other device");
    facts.DeviceDestroyed(other);Check(!Allowed(facts,other,c,z),"all target device facts removed");
}
static void SameHandlesAcrossDevices(){
    VkNrImageFacts facts;const auto d=Handle<VkDevice>(1),other=Handle<VkDevice>(2),unknown=Handle<VkDevice>(3);
    const auto a=Handle<VkImage>(100),b=Handle<VkImage>(200);
    const auto x=Handle<VkImageView>(1000),y=Handle<VkImageView>(1001);
    auto larger=ImageInfo();larger.extent.width=64;
    facts.Created(d,ImageInfo(),a,VK_SUCCESS);
    facts.ViewCreated(d,ViewInfo(a),x,VK_SUCCESS);facts.ViewCreated(d,ViewInfo(a),y,VK_SUCCESS);
    const auto original=facts.Image(d,a);
    Check(original.has_value(),"first device image observed");
    facts.Created(other,larger,a,VK_SUCCESS);
    facts.ViewCreated(other,ViewInfo(a),x,VK_SUCCESS);facts.ViewCreated(other,ViewInfo(a),y,VK_SUCCESS);
    Check(Allowed(facts,d,a,x)&&Allowed(facts,d,a,y),"second device same image/view handles preserve first device rights");
    Check(Allowed(facts,other,a,x)&&Allowed(facts,other,a,y),"second device same handles have independent rights");
    const auto first=facts.Image(d,a),second=facts.Image(other,a);
    Check(first&&second&&first->generation==original->generation&&first->generation!=second->generation&&
          first->extent.width==32&&second->extent.width==64,"image lookup preserves device-specific metadata and generation");
    Check(!facts.Image(unknown,a)&&!Allowed(facts,unknown,a,x),"unobserved device cannot use colliding handles");
    Check(facts.Describe(d,Resource(a,x)).find("extent=32x32x1")!=std::string::npos&&
          facts.Describe(other,Resource(a,x)).find("extent=64x32x1")!=std::string::npos,
          "diagnostics describe the requested device image");

    facts.ViewDestroyed(d,x);
    Check(!Allowed(facts,d,a,x)&&Allowed(facts,other,a,x),"view destruction is device scoped");
    facts.ViewCreated(d,ViewInfo(a),x,VK_SUCCESS);
    facts.Created(d,ImageInfo(),b,VK_SUCCESS);
    facts.ViewCreated(d,ViewInfo(b),x,VK_SUCCESS);
    facts.Destroyed(d,a);
    Check(Allowed(facts,d,b,x)&&!Allowed(facts,d,a,y)&&Allowed(facts,other,a,x)&&Allowed(facts,other,a,y),
          "image destruction preserves reassigned view and other device reverse index");
    facts.Created(d,ImageInfo(),a,VK_SUCCESS);
    Check(!Allowed(facts,d,a,y)&&Allowed(facts,other,a,y),"image recreation cannot revive a previous view or erase another device view");
    facts.ViewCreated(d,ViewInfo(a),y,VK_SUCCESS);
    const auto otherGeneration=facts.Image(other,a)->generation;
    facts.Created(d,ImageInfo(),a,VK_SUCCESS);
    Check(!Allowed(facts,d,a,y)&&Allowed(facts,other,a,y)&&facts.Image(other,a)->generation==otherGeneration,
          "duplicate image observation invalidates only the device's prior generation");
    facts.ViewCreated(d,ViewInfo(a),y,VK_SUCCESS);
    facts.ViewDestroyed(other,y);
    Check(Allowed(facts,d,a,y)&&!Allowed(facts,other,a,y),"interleaved view erase keeps other device reverse index");
    facts.DeviceDestroyed(other);facts.DeviceDestroyed(other);
    Check(!facts.Image(other,a)&&!Allowed(facts,other,a,x)&&Allowed(facts,d,a,y)&&Allowed(facts,d,b,x),
          "device destruction removes only its images and indexed views");
    facts.Created(other,larger,a,VK_SUCCESS);
    Check(!Allowed(facts,other,a,x),"device recreation does not revive views");
    facts.ViewCreated(other,ViewInfo(a),x,VK_SUCCESS);
    Check(facts.Image(other,a)->generation!=otherGeneration&&Allowed(facts,other,a,x),"device recreation gets fresh rights and generation");
    facts.DeviceDestroyed(d);
    Check(!Allowed(facts,d,a,y)&&!Allowed(facts,d,b,x)&&Allowed(facts,other,a,x),"reverse-order device teardown preserves colliding peer view");
    facts.Destroyed(other,a);
    Check(!Allowed(facts,other,a,x),"recreated device image cleanup removes its view");
}
static double Churn(unsigned background){
    VkNrImageFacts facts;const auto d=Handle<VkDevice>(1);const auto ci=ImageInfo();
    for(unsigned i=0;i<background;++i){auto im=Handle<VkImage>(100+i);facts.Created(d,ci,im,VK_SUCCESS);
        facts.ViewCreated(d,ViewInfo(im),Handle<VkImageView>(100000+i),VK_SUCCESS);}
    const auto target=Handle<VkImage>(900000);const auto vi=ViewInfo(target);
    double best=1e30;
    for(int trial=0;trial<3;++trial){const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<1024;++i){facts.Created(d,ci,target,VK_SUCCESS);
            facts.ViewCreated(d,vi,Handle<VkImageView>(900001),VK_SUCCESS);facts.Destroyed(d,target);}
        best=std::min(best,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
    return best;
}
int main(){Lifetime();SameHandlesAcrossDevices();const auto small=Churn(256),large=Churn(65536);
    std::printf("1024 image/view create+destroy cycles: 256 unrelated views %.3f ms; 65536 unrelated views %.3f ms\n",small,large);
    Check(large<small*8.0+5.0,"image destruction cost grows with unrelated views");
    std::puts("PASS: lifetime, generation, view reassignment, device separation and cleanup scaling");
}
