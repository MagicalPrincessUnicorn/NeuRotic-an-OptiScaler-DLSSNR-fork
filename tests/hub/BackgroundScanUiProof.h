#pragma once
#include <fstream>
#include <map>
// Test-only capture of actual ImGui draw data for a CPU-rendered UI proof.
// This runs without a graphics device, installer operation or game launch.
inline void CaptureBackgroundScanUiProof(const char* name){
 const auto* data=ImGui::GetDrawData();if(!data)return;
 auto directory=nh::UserRoot()/L"background-scan-ui-proof";std::filesystem::create_directories(directory);
 nh::Json proof={{"display",{data->DisplaySize.x,data->DisplaySize.y}},{"origin",{data->DisplayPos.x,data->DisplayPos.y}},{"lists",nh::Json::array()},{"textures",nh::Json::array()}};
 std::map<const ImTextureData*,int> textures;
 for(int i=0;i<data->CmdListsCount;++i){const auto* list=data->CmdLists[i];nh::Json vertices=nh::Json::array(),indices=nh::Json::array(),commands=nh::Json::array();
  for(const auto& vertex:list->VtxBuffer)vertices.push_back({vertex.pos.x,vertex.pos.y,vertex.uv.x,vertex.uv.y,vertex.col});
  for(auto index:list->IdxBuffer)indices.push_back(index);
  for(const auto& cmd:list->CmdBuffer){
   if(cmd.UserCallback)throw std::runtime_error("UI proof cannot omit a rendering callback");
   const auto* texture=cmd.TexRef._TexData;int textureIndex=-1;
   if(texture){auto [found,inserted]=textures.try_emplace(texture,(int)textures.size());textureIndex=found->second;if(inserted){
    if(!texture->Pixels||(texture->BytesPerPixel!=1&&texture->BytesPerPixel!=4))throw std::runtime_error("UI proof requires actual texture pixels");
    auto file=std::string(name)+"-texture-"+std::to_string(textureIndex)+".bin";std::ofstream pixels(directory/file,std::ios::binary);pixels.write((const char*)texture->Pixels,texture->GetSizeInBytes());if(!pixels)throw std::runtime_error("UI proof texture write failed");
    proof["textures"].push_back({{"file",file},{"width",texture->Width},{"height",texture->Height},{"bytesPerPixel",texture->BytesPerPixel}});
   }}else if(cmd.TexRef._TexID!=ImTextureID_Invalid)throw std::runtime_error("UI proof cannot substitute external image pixels");
   commands.push_back({{"count",cmd.ElemCount},{"indexOffset",cmd.IdxOffset},{"vertexOffset",cmd.VtxOffset},{"clip",{cmd.ClipRect.x,cmd.ClipRect.y,cmd.ClipRect.z,cmd.ClipRect.w}},{"texture",textureIndex}});
  }
  proof["lists"].push_back({{"vertices",std::move(vertices)},{"indices",std::move(indices)},{"commands",std::move(commands)}});
 }
 std::ofstream output(directory/(std::string(name)+".json"));output<<proof.dump();if(!output)throw std::runtime_error("UI proof geometry write failed");
}
