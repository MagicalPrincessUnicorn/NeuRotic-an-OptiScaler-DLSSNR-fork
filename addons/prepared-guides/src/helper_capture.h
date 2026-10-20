#pragma once
#include <nr/context/PreparedRasterBounds.h>
#include "../helper/PreparedHelperClient.h"
#include "capture_codec.h"
#include "capture_input.h"
#include <reshade.hpp>
#include <array>
#include <cstdio>
namespace nrpg {
namespace A=reshade::api;
namespace H=Neurotic::Feed::Prepared::Helper;
inline std::string Config(A::effect_runtime* r,const char* key){char value[32768]{};size_t size=sizeof(value);if(!reshade::get_config_value(r,"NeuRoticPreparedGuides",key,value,&size))return {};return value;}
inline std::wstring Wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(!n)return {};std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);return out;}
// Caller retains this bounded slot if a source queue completion cannot be proved.
// API object destruction occurs only while the source device is alive and idle.
class HelperCapture {
    A::device* device_=nullptr;std::array<A::resource,4> staging_{};std::array<A::resource_desc,4> shapes_{};
    std::array<std::uint32_t,4> pitches_{};bool buffers_=false,bottomLeft_=false;H::Client client_;bool started_=false;
public:
    void Dispose(){if(device_)for(auto r:staging_)if(r.handle)device_->destroy_resource(r);staging_={};device_=nullptr;client_.Stop();}
    bool Start(A::effect_runtime* runtime,std::string& reason){
        if(started_)return client_.Alive();H::ClientOptions options;options.executable=Wide(Config(runtime,"HelperExe"));options.model=Wide(Config(runtime,"Model"));
        options.forwarder=Wide(Config(runtime,"Forwarder"));options.ngxPath=Wide(Config(runtime,"NgxPath"));options.adapterLuid=Wide(Config(runtime,"AdapterLuid"));
        options.colorStrength=std::strtof(Config(runtime,"ColorStrength").c_str(),nullptr);options.probeContent=Config(runtime,"ProbeContent")=="1";
        // ReShade's device property is the actual source adapter LUID. OpenGL
        // implementations without that property require explicit CPU-route selection.
        std::uint64_t luid=0;if(runtime->get_device()->get_property(A::device_properties::adapter_luid,&luid)&&luid){
            char text[32]{};std::snprintf(text,sizeof(text),"%08x:%08x",unsigned(luid>>32),unsigned(luid));options.adapterLuid=Wide(text);}
        started_=client_.Start(options,reason);return started_;
    }
    bool Allocate(A::device* device,const std::array<A::resource,4>& source,std::string& reason,const std::array<A::resource_view,4>& views={}){
        std::array<A::resource_desc,4> shapes{};for(unsigned i=0;i<4;++i){if(!source[i].handle){reason="Capture texture binding missing";return false;}shapes[i]=device->get_resource_desc(source[i]);
            if(views[i].handle && (device->get_resource_from_view(views[i])!=source[i] || !TypedCaptureShape(shapes[i],device->get_resource_view_desc(views[i])))){reason="Capture typed view does not cover its resource";return false;}}
        auto w=shapes[0].texture.width,h=shapes[0].texture.height;if(!Neurotic::PreparedRaster::Admit(w,h)||std::uint64_t(w)*h>H::MaxPayloadBytes/20){reason="Capture dimensions or payload exceed supported bounds";return false;}
        for(const auto& d:shapes)if(d.type!=A::resource_type::texture_2d||d.texture.width!=w||d.texture.height!=h||d.texture.samples!=1||d.texture.depth_or_layers!=1||d.texture.levels!=1){reason="Capture textures must match full-size single-subresource coverage";return false;}
        const auto color=shapes[0].texture.format,motion=shapes[2].texture.format;
        if((color!=A::format::r8g8b8a8_unorm&&color!=A::format::b8g8r8a8_unorm)||shapes[1].texture.format!=A::format::r32_float||
           (motion!=A::format::r16g16_float&&motion!=A::format::r32g32_float)||shapes[3].texture.format!=A::format::r8_unorm){reason="Helper requires typed SDR RGBA/BGRA8, R32 depth, RG16/32 motion, R8 distrust";return false;}
        bool same=device_==device;for(unsigned i=0;i<4;++i)same=same&&staging_[i].handle&&shapes_[i].texture.width==w&&shapes_[i].texture.height==h&&shapes_[i].texture.format==shapes[i].texture.format;if(same)return true;
        if(device_){for(auto r:staging_)if(r.handle)device_->destroy_resource(r);}staging_={};device_=device;shapes_=shapes;
        bottomLeft_=device->get_api()==A::device_api::opengl;buffers_=device->check_capability(A::device_caps::copy_buffer_to_texture);
        std::uint64_t total=0;for(unsigned i=0;i<4;++i){unsigned bpp=i==3?1:i==2&&motion==A::format::r32g32_float?8:4;pitches_[i]=Pitch(w,bpp);total+=std::uint64_t(pitches_[i])*h;}
        if(total>H::MaxPayloadBytes){reason="Capture staging exceeds payload bound";return false;}
        for(unsigned i=0;i<4;++i){A::resource_desc d= buffers_?A::resource_desc(std::uint64_t(pitches_[i])*h,A::memory_heap::readback,A::resource_usage::copy_dest):
            A::resource_desc(w,h,1,1,shapes[i].texture.format,1,A::memory_heap::readback,A::resource_usage::copy_dest);
            if(!device->create_resource(d,nullptr,A::resource_usage::copy_dest,&staging_[i])){reason="Completed CPU readback allocation unavailable";return false;}}
        return true;
    }
    void Record(A::command_list* commands,const std::array<A::resource,4>& source){for(unsigned i=0;i<4;++i){auto state=i?A::resource_usage::shader_resource:A::resource_usage::render_target;
        commands->barrier(source[i],state,A::resource_usage::copy_source);unsigned bpp=i==3?1:i==2&&shapes_[i].texture.format==A::format::r32g32_float?8:4;
        if(buffers_)commands->copy_texture_to_buffer(source[i],0,nullptr,staging_[i],0,pitches_[i]/bpp,shapes_[i].texture.height);else commands->copy_resource(source[i],staging_[i]);
        commands->barrier(source[i],A::resource_usage::copy_source,state);}}
    void RecordColor(A::command_list* commands,A::resource source,A::resource_usage state){
        commands->barrier(source,state,A::resource_usage::copy_source);
        if(buffers_)commands->copy_texture_to_buffer(source,0,nullptr,staging_[0],0,pitches_[0]/4,shapes_[0].texture.height);else commands->copy_resource(source,staging_[0]);
        commands->barrier(source,A::resource_usage::copy_source,state);
    }
    bool ColorShape(A::resource resource)const{auto d=device_->get_resource_desc(resource);return d.texture.width==shapes_[0].texture.width&&d.texture.height==shapes_[0].texture.height&&
        (d.texture.format==shapes_[0].texture.format || (A::format_is_typeless(d.texture.format)&&A::format_to_typeless(d.texture.format)==A::format_to_typeless(shapes_[0].texture.format)))&&d.texture.samples==1;}
    bool Pack(std::vector<std::byte>& packed){
        const auto w=shapes_[0].texture.width,h=shapes_[0].texture.height;packed.resize(std::size_t(w)*h*20);size_t offset=0;
        for(unsigned i=0;i<4;++i){A::subresource_data map{};void* ptr=nullptr;
            bool mapped=buffers_?device_->map_buffer_region(staging_[i],0,std::uint64_t(pitches_[i])*h,A::map_access::read_only,&ptr):device_->map_texture_region(staging_[i],0,nullptr,A::map_access::read_only,&map);
            if(!mapped)return false;if(buffers_){map.data=ptr;map.row_pitch=pitches_[i];}unsigned bytes=i==3?1:i==2&&shapes_[i].texture.format==A::format::r32g32_float?8:4;
            if(!map.data||map.row_pitch<w*bytes){if(buffers_)device_->unmap_buffer_region(staging_[i]);else device_->unmap_texture_region(staging_[i],0);return false;}
            for(unsigned y=0;y<h;++y){auto* row=static_cast<const std::uint8_t*>(map.data)+std::size_t(NativeRow(y,h,bottomLeft_))*map.row_pitch;
                for(unsigned x=0;x<w;++x){if(i==0){std::uint8_t pixel[4];std::memcpy(pixel,row+x*4,4);if(shapes_[0].texture.format==A::format::r8g8b8a8_unorm)std::swap(pixel[0],pixel[2]);std::memcpy(packed.data()+offset,pixel,4);offset+=4;}
                    else if(i==1){std::memcpy(packed.data()+offset,row+x*4,4);offset+=4;}
                    else {float values[2]{};if(i==3)values[0]=row[x]/255.f;else if(bytes==8)std::memcpy(values,row+x*8,8);else{std::uint16_t v[2];std::memcpy(v,row+x*4,4);values[0]=Half(v[0]);values[1]=Half(v[1]);}
                        if(i==2&&bottomLeft_)values[1]=-values[1];unsigned n=i==3?4:8;std::memcpy(packed.data()+offset,values,n);offset+=n;}}}
            if(buffers_)device_->unmap_buffer_region(staging_[i]);else device_->unmap_texture_region(staging_[i],0);
        }return true;
    }
    bool Process(const H::Request& request,std::span<const std::byte> packed,H::Reply& reply,std::vector<std::uint8_t>& out,std::string& reason){return client_.Process(request,packed,reply,out,reason)&&H::CompletedReply(reply,request);}
    void Upload(A::command_list* commands,A::command_queue* queue,A::resource destination,std::vector<std::uint8_t>& bgra){
        auto w=shapes_[0].texture.width,h=shapes_[0].texture.height;if(shapes_[0].texture.format==A::format::r8g8b8a8_unorm)for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
        if(bottomLeft_)for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w*4;++x)std::swap(bgra[std::size_t(y)*w*4+x],bgra[std::size_t(h-1-y)*w*4+x]);
        A::subresource_data data{bgra.data(),w*4,w*h*4};commands->barrier(destination,A::resource_usage::render_target,A::resource_usage::copy_dest);
        // Device upload is immediate; flush the recorded transition before it.
        // Caller establishes source completion again before releasing CPU bytes.
        if(device_->check_capability(A::device_caps::update_texture_region_command))commands->update_texture_region(data,destination,0);
        else {queue->flush_immediate_command_list();queue->wait_idle();device_->update_texture_region(data,destination,0);}commands->barrier(destination,A::resource_usage::copy_dest,A::resource_usage::render_target);
    }
};
}
