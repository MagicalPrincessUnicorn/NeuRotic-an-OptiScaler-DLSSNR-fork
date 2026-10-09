#include "ui/HubViewModel.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct WindowState {
 ImGuiWindow* window;
 ImVec2 position,scroll;
};

class ScrollFixture {
public:
 nh::HubModel model;
 int failed=0;
 float dpi;
 std::string description;

 ScrollFixture(ImVec2 logicalSize,float scale):dpi(scale){
  description=std::to_string((int)logicalSize.x)+"x"+std::to_string((int)logicalSize.y)+" logical, DPI "+std::to_string(scale);
  ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
  io.DisplaySize={logicalSize.x*dpi,logicalSize.y*dpi};io.DeltaTime=1.f/60;
  io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
  io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16);
  nh::ApplySharedTheme(false);ImGui::GetStyle().ScaleAllSizes(dpi);io.FontGlobalScale=dpi;
  model.reducedMotion=true;model.onlineArtwork=false;
  for(int i=0;i<120;++i)model.games.push_back({"scroll-"+std::to_string(i),"Fixture game "+std::to_string(i),"Custom","C:/fixture",{}});
  model.selected=0;
 }
 ~ScrollFixture(){ImGui::DestroyContext();}

 void Check(bool condition,const std::string& message){
  std::cout<<(condition?"PASS ":"FAIL ")<<"NH-SCROLL: "<<message<<" ["<<description<<"]"<<std::endl;
  failed+=!condition;
 }
 void Render(){ImGui::NewFrame();nh::RenderHub(model,nullptr,dpi);ImGui::Render();}
 void Settle(){for(int i=0;i<4;++i)Render();}
 bool ItemBounds(ImGuiWindow* window,ImGuiID id,ImRect& bounds){
  if(!window||!id)return false;
  auto previousWindow=GImGui->NavWindow;auto previousId=GImGui->NavId;
  GImGui->NavWindow=window;GImGui->NavId=id;Render();
  const bool submitted=GImGui->NavIdIsAlive&&GImGui->NavId==id;
  bounds=ImGui::WindowRectRelToAbs(window,window->NavRectRel[ImGuiNavLayer_Main]);
  GImGui->NavWindow=previousWindow;GImGui->NavId=previousId;
  return submitted&&bounds.GetWidth()>1&&bounds.GetHeight()>1;
 }
 ImGuiWindow* Window(const char* name){
  if(std::strcmp(name,"NeuRotic")==0)return ImGui::FindWindowByName(name);
  for(auto window:GImGui->Windows)if(window->Active&&std::strstr(window->Name,name))return window;
  return nullptr;
 }
 std::vector<WindowState> Capture(std::initializer_list<const char*> names){
  std::vector<WindowState> result;
  for(auto name:names)if(auto window=Window(name))result.push_back({window,window->Pos,window->Scroll});
  return result;
 }
 bool Unchanged(const std::vector<WindowState>& states){
  return std::all_of(states.begin(),states.end(),[](const WindowState& s){
   return std::abs(s.window->Scroll.x-s.scroll.x)<.5f&&std::abs(s.window->Scroll.y-s.scroll.y)<.5f&&
          std::abs(s.window->Pos.x-s.position.x)<.5f&&std::abs(s.window->Pos.y-s.position.y)<.5f;
  });
 }
 std::vector<WindowState> Fixed(){
  return Capture({"NeuRotic","PageContent","Navigation","HeaderReadiness","HeaderActivity","FooterStatus","LibraryHeader","LibraryColumn"});
 }
 // Find a visible piece of the real window that is not covered by a child.
 // Validate ImGui's hit test, then deliver wheel events through its public IO.
 bool Hover(ImGuiWindow* window,bool allowWheelForwarding=false){
  if(!window)return false;
  auto& io=ImGui::GetIO();io.AddMousePosEvent(-1000,-1000);Render();
  const auto rect=window->OuterRectClipped;
  const float fractions[]={.02f,.15f,.35f,.5f,.75f,.98f};
  for(float y:fractions)for(float x:fractions){
   io.AddMousePosEvent(rect.Min.x+rect.GetWidth()*x,rect.Min.y+rect.GetHeight()*y);Render();
   auto hovered=GImGui->HoveredWindow;
   if(allowWheelForwarding)while(hovered&&hovered!=window&&(hovered->Flags&ImGuiWindowFlags_ChildWindow)){
    const bool forwards=(hovered->ScrollMax.y==0)||((hovered->Flags&ImGuiWindowFlags_NoScrollWithMouse)&&!(hovered->Flags&ImGuiWindowFlags_NoMouseInputs));
    if(!forwards)break;hovered=hovered->ParentWindow;
   }
   if(hovered==window)return true;
  }
  return false;
 }
 bool Wheel(ImGuiWindow* window,float amount,int repetitions=1){
  if(!Hover(window,true))return false;
  for(int i=0;i<repetitions;++i){ImGui::GetIO().AddMouseWheelEvent(0,amount);Render();Render();}
  return true;
 }
 bool Within(ImGuiWindow* child,ImGuiWindow* parent){
  if(!child||!parent)return false;
  const auto& bounds=parent->InnerRect;
  return child->Pos.x>=bounds.Min.x-2&&child->Pos.y>=bounds.Min.y-2&&
         child->Pos.x+child->Size.x<=bounds.Max.x+2&&child->Pos.y+child->Size.y<=bounds.Max.y+2;
 }
 void FixedInput(const char* name,const char* label){
  auto target=Window(name);auto before=Fixed();
  const bool delivered=Wheel(target,-6,3)&&Wheel(target,6,3);
  Check(delivered&&Unchanged(before),label);
 }
 void Boundaries(ImGuiWindow* leaf,const char* label){
  auto before=Fixed();
  const bool bottom=Wheel(leaf,-10000)&&leaf&&std::abs(leaf->Scroll.y-leaf->ScrollMax.y)<.5f;
  const bool beyondBottom=Wheel(leaf,-6,3);
  const bool top=Wheel(leaf,10000)&&leaf&&leaf->Scroll.y<.5f;
  const bool beyondTop=Wheel(leaf,6,3);
  Check(bottom&&beyondBottom&&top&&beyondTop&&Unchanged(before),label);
  if(!(bottom&&beyondBottom&&top&&beyondTop&&Unchanged(before))){std::cout<<"SCROLL-BOUNDARY bottom="<<bottom<<" beyondBottom="<<beyondBottom<<" top="<<top<<" beyondTop="<<beyondTop<<" fixed="<<Unchanged(before)<<" leaf="<<(leaf?leaf->Name:"missing")<<" scroll="<<(leaf?leaf->Scroll.y:-1)<<" max="<<(leaf?leaf->ScrollMax.y:-1)<<std::endl;for(auto& state:before)if(std::abs(state.window->Scroll.y-state.scroll.y)>=.5f||std::abs(state.window->Pos.y-state.position.y)>=.5f)std::cout<<"SCROLL-MOVED "<<state.window->Name<<" y="<<state.position.y<<"->"<<state.window->Pos.y<<" scroll="<<state.scroll.y<<"->"<<state.window->Scroll.y<<std::endl;}
 }
 bool ClickRail(){
  auto page=Window("PageContent"),details=Window("GameDetails"),column=Window("LibraryColumn");if(!page||!details)return false;
  const auto id=page->GetID("##LibraryDrawer");auto& io=ImGui::GetIO();float left=column?column->Pos.x+column->Size.x:page->InnerRect.Min.x;
  float right=details->Pos.x,y=details->OuterRectClipped.GetCenter().y;bool found=false;
  for(float x=left+1;x<right;x+=2*dpi){io.AddMousePosEvent(x,y);Render();if(GImGui->HoveredId==id){found=true;break;}}
  if(!found)return false;
  io.AddMouseButtonEvent(0,true);Render();io.AddMouseButtonEvent(0,false);Render();Settle();return true;
 }
 void Drawer(){
  auto details=Window("GameDetails"),column=Window("LibraryColumn");if(!details||!column){Check(false,"Library drawer has both initial panes");return;}
  const float initialWidth=details->Size.x,columnWidth=column->Size.x;const int selected=model.selected;const size_t count=model.games.size();
  const bool collapsed=ClickRail();details=Window("GameDetails");
  Check(collapsed&&!Window("LibraryColumn")&&!Window("LibraryList")&&details&&details->Size.x>=initialWidth+columnWidth-2*dpi&&model.selected==selected&&model.games.size()==count,
        "clicking the vertical rail hides the list and gives its space to the same selected game");
  const bool restored=ClickRail();details=Window("GameDetails");
  Check(restored&&Window("LibraryColumn")&&Window("LibraryList")&&details&&std::abs(details->Size.x-initialWidth)<2*dpi&&model.selected==selected,
        "clicking the collapsed rail restores the list without changing the selected game");
 }
 void HeaderControls(){
  model.page=0;Settle();auto root=Window("NeuRotic");ImRect theme,anything,arrow;
  const bool found=root&&ItemBounds(root,root->GetID("##ThemeToggle"),theme)&&ItemBounds(root,root->GetID("##NRAnythingControl"),anything)&&ItemBounds(root,root->GetID("##WindowDropdown"),arrow);
  Check(found&&std::abs(theme.GetWidth()-theme.GetHeight())<1&&std::abs(theme.Min.y-anything.Min.y)<1&&std::abs(theme.Max.y-anything.Max.y)<1&&std::abs(arrow.Min.y-anything.Min.y)<1&&std::abs(arrow.Max.y-anything.Max.y)<1,
        "theme, NR Anything action and selector share a centered uniform height; theme is square");
  Check(found&&anything.Min.x-theme.Max.x>=20*dpi&&arrow.Max.x<=root->InnerClipRect.Max.x+.5f&&theme.Min.y>=root->InnerClipRect.Min.y&&anything.Max.y<Window("Navigation")->Pos.y,
        "header action fits the viewport and leaves space for its divider above navigation");
 }
 void Home(){
  model.preflight={{"runtime",{{"ready",false}}}};
  model.page=0;Settle();
  auto page=Window("PageContent"),root=Window("NeuRotic");
  std::vector<ImGuiWindow*> cards;
  for(auto candidate:GImGui->Windows)if(candidate->Active&&std::strstr(candidate->Name,"ComponentCard"))cards.push_back(candidate);
  std::sort(cards.begin(),cards.end(),[](auto left,auto right){return left->Pos.x<right->Pos.x;});
  bool aligned=cards.size()==3;
  if(aligned)for(auto card:cards)aligned&=Within(card,page)&&std::abs(card->Pos.y-cards[0]->Pos.y)<1&&std::abs(card->Size.x-cards[0]->Size.x)<1;
  Check(aligned&&!Window("HeaderReadiness"),"Home shows exactly three equal-width component cards on one row without duplicate readiness boxes");
  Check(page&&root&&page->ScrollMax.x==0&&page->ScrollMax.y==0&&root->ScrollMax.x==0&&root->ScrollMax.y==0,
        "Home component cards do not add horizontal or vertical scrolling to fixed parents");
  if(cards.size()==3&&page){
   ImRect actions[3];bool actionFit=true;
   for(int i=0;i<3;++i){
    auto card=cards[i];const char* label=i==0?"    Download Visual C++":"    Open Folder";
    const bool actionFound=ItemBounds(card,card->GetID(label),actions[i]);
    actionFit&=actionFound&&actions[i].Min.x>=card->InnerClipRect.Min.x-.5f&&actions[i].Min.y>=card->InnerClipRect.Min.y-.5f&&
     actions[i].Max.x<=card->InnerClipRect.Max.x+.5f&&actions[i].Max.y<=card->InnerClipRect.Max.y+.5f;
    if(!actionFound||!actionFit)std::cout<<"HOME-CARD "<<i<<" found="<<actionFound
     <<" card="<<card->Pos.x<<','<<card->Pos.y<<'/'<<card->Size.x<<','<<card->Size.y
     <<" action="<<actions[i].Min.x<<','<<actions[i].Min.y<<'/'<<actions[i].Max.x<<','<<actions[i].Max.y<<std::endl;
   }
   Check(actionFit&&std::abs(actions[1].GetWidth()-actions[2].GetWidth())<1,
         "component action buttons fit their cards and both Open Folder buttons have equal widths");
   auto intro=Window("HomeIntroduction");ImRect refresh;
   const bool found=intro&&ItemBounds(intro,intro->GetID("##RefreshHomeComponents"),refresh);
   Check(found&&Within(intro,page)&&refresh.Min.y>=intro->InnerClipRect.Min.y-.5f&&refresh.Max.y<=intro->InnerClipRect.Max.y+.5f&&
         std::abs(refresh.Max.x-intro->InnerRect.Max.x)<1&&refresh.Max.y<=cards[0]->Pos.y,
         "shared component Refresh is contained at the right edge of the introductory row above all three cards");
  }
  auto directory=Window("DirectoryManager");
  Check(Within(directory,Window("PageContent"))&&directory&&directory->InnerRect.GetHeight()>=30*dpi,
        "Home directory area stays contained with at least 30 logical pixels of useful height");
  FixedInput("NeuRotic","Home root ignores wheel input in both directions");
  FixedInput("PageContent","noninteractive Home content cannot scroll the page or header");
  FixedInput("DirectoryManager","empty Home directory area cannot bubble wheel input into fixed ancestors");
  for(auto name:{"Navigation","HeaderReadiness","HeaderActivity","FooterStatus"})
   if(Window(name))FixedInput(name,(std::string(name)+" ignores wheel input and leaves fixed panes still").c_str());

  const auto previousMessage=model.message;
  model.message="Full footer message: ";
  for(int i=0;i<12;++i)model.message+="This status contains more detail than the fixed footer can display. ";
  model.message+="COMPLETE STATUS END.";
  Settle();auto footer=Window("FooterStatus");auto fixed=Fixed();const bool hovered=Hover(footer);
  ImGui::NewFrame();ImGui::LogToBuffer();nh::RenderHub(model,nullptr,dpi);
  const std::string rendered=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::Render();
  auto tooltip=Window("##Tooltip");const auto first=rendered.find(model.message);
  const bool fullTooltip=first!=std::string::npos&&rendered.find(model.message,first+model.message.size())!=std::string::npos;
  Check(hovered&&tooltip&&tooltip->DrawList->VtxBuffer.Size>0&&fullTooltip&&Unchanged(fixed),
        "hovering the ellipsized footer renders its complete message in a tooltip without moving panes");
  model.message=previousMessage;ImGui::GetIO().AddMousePosEvent(-1000,-1000);Settle();
 }
 void LibraryToolbar(){
  model.page=1;Settle();auto header=Window("LibraryHeader");
  ImRect filter,search,add,directory,scan;
  const bool found=header&&ItemBounds(header,header->GetID("##LibraryOrder"),filter)&&
   ItemBounds(header,header->GetID("##Search"),search)&&ItemBounds(header,header->GetID("+ Add game"),add)&&
   ItemBounds(header,header->GetID("    Add directory"),directory)&&ItemBounds(header,header->GetID("    Scan"),scan);
  auto contained=[&](const ImRect& r){return header&&r.Min.x>=header->InnerClipRect.Min.x-.5f&&r.Max.x<=header->InnerClipRect.Max.x+.5f&&r.Min.y>=header->InnerClipRect.Min.y-.5f&&r.Max.y<=header->InnerClipRect.Max.y+.5f;};
  Check(found&&contained(filter)&&contained(search)&&contained(add)&&contained(directory)&&contained(scan),
   "Library filter, search and all actions are fully contained");
  const bool wide=ImGui::GetIO().DisplaySize.x/dpi>=1200;
  if(wide){
   Check(found&&filter.Min.x<search.Min.x&&search.Min.x<add.Min.x&&add.Min.x<directory.Min.x&&directory.Min.x<scan.Min.x&&
    std::abs(filter.Min.y-search.Min.y)<1&&std::abs(search.Min.y-add.Min.y)<1&&std::abs(add.Min.y-directory.Min.y)<1&&std::abs(directory.Min.y-scan.Min.y)<1,
    "wide Library has one coherent Filter Search Add game Add directory Scan row");
   Check(found&&search.GetWidth()>=220*dpi&&header->Size.y<=70*dpi,
    "wide Library reserves readable search and reclaims redundant header space");
  }
  const auto before=Capture({"LibraryHeader","LibraryColumn","GameDetails"});const ImVec2 size=header?header->Size:ImVec2{};
  model.discovery.busy=true;Settle();header=Window("LibraryHeader");
  Check(header&&std::abs(header->Size.x-size.x)<.5f&&std::abs(header->Size.y-size.y)<.5f&&Unchanged(before),
   "Scanning state keeps toolbar and list/details geometry stable");
  model.discovery.busy=false;Settle();
  // Deliver real mouse/text input to the preserved search control.
  if(found){
   auto& io=ImGui::GetIO();io.AddMousePosEvent(search.GetCenter().x,search.GetCenter().y);Render();
   io.AddMouseButtonEvent(0,true);Render();io.AddMouseButtonEvent(0,false);Render();
   io.AddInputCharactersUTF8("Fixture game 1");Render();Render();
   Check(std::string(model.search.data())=="Fixture game 1"&&model.OrderedGames(model.search.data()).size()==31,
    "real keyboard input filters games through the preserved search control");
   io.AddKeyEvent(ImGuiKey_Escape,true);Render();io.AddKeyEvent(ImGuiKey_Escape,false);Render();
   model.search.fill(0);io.AddMousePosEvent(-1000,-1000);Settle();
  }
 }
 void Library(bool clamped=false){
  model.page=1;Settle();
  auto root=Window("NeuRotic"),page=Window("PageContent"),column=Window("LibraryColumn");
  auto list=Window("LibraryList"),details=Window("GameDetails");
  Check(Within(page,root)&&Within(Window("Navigation"),root)&&!Window("HeaderReadiness")&&
        (!Window("HeaderActivity")||Within(Window("HeaderActivity"),root))&&Within(Window("FooterStatus"),root),
        "content, navigation, readiness, activity and footer fit the viewport");
  Check(Within(Window("LibraryHeader"),page)&&Within(column,page)&&Within(details,page)&&Within(list,column),
        "Library header, list column and details fit their containing panes");
  if(clamped)Check(list&&details&&list->InnerRect.GetHeight()>=50*dpi&&details->InnerRect.GetHeight()>=50*dpi,
        "work-area-clamped Library list and details retain at least 50 logical pixels of useful height");
  FixedInput("NeuRotic","Library root ignores wheel input without shifting panes");
  FixedInput("PageContent","Library outer content ignores wheel input without shifting panes");
  FixedInput("LibraryHeader","Library header wheel cannot move its content or the outer page");
  const auto still=Fixed();const float detailsBefore=details?details->Scroll.y:0;
  const bool listInput=Wheel(list,-6,3);
  Check(listInput&&list&&list->ScrollMax.y>0&&list->Scroll.y>1&&details&&details->Scroll.y==detailsBefore&&Unchanged(still),
        "crowded Library list scrolls while the details and fixed LibraryColumn stay still");
  Boundaries(list,"Library list top and bottom wheel boundaries stay inside the list");
  Drawer();

  // A legitimate long executable refusal gives GameDetails real overflow at
  // every tested size, independent of an arbitrary fixed child height.
  for(int i=0;i<35;++i)model.games[0].target.reason+="Fixture executable needs review before installation.\n";
  Settle();details=Window("GameDetails");list=Window("LibraryList");auto selectedHeader=Window("SelectedGameHeader");const float headerBefore=selectedHeader?selectedHeader->Pos.y:0;
  const auto fixed=Fixed();const float listBefore=list?list->Scroll.y:0;
  const bool detailsInput=Wheel(details,-6,3);
  Check(detailsInput&&details&&details->ScrollMax.y>0&&details->Scroll.y>1&&list&&list->Scroll.y==listBefore&&Unchanged(fixed),
        "tall game details scroll independently of the list and outer page");
  Check(selectedHeader&&selectedHeader->Pos.y<headerBefore&&!Window("InstallationScroll")&&!Window("SettingsScroll")&&!Window("DiagnosticsScroll"),
        "the selected-game header moves with the single right-side scroll owner");
  Boundaries(details,"game details top and bottom wheel boundaries leave fixed ancestors still");
 }
 void Run(){
  HeaderControls();Home();LibraryToolbar();Library();
  model.page=3;model.preflightError.clear();
  for(int i=0;i<45;++i)model.preflightError+="Fixture component status explanation.\n";
  Settle();auto body=Window("PageBody");auto fixedPage=Fixed();
  Check(Within(body,Window("PageContent")),"Components scrolling body fits the fixed content pane");
  Check(Wheel(body,-6,3)&&body&&body->ScrollMax.y>0&&body->Scroll.y>1&&Unchanged(fixedPage),
        "long Components content scrolls only its PageBody");
  Boundaries(body,"Components body wheel boundaries cannot scroll the App frame");
  for(int selectedPage:{4,5}){
   model.page=selectedPage;Settle();
   Check(Within(Window("PageBody"),Window("PageContent")),"Settings and Diagnostics use a contained scrolling body");
  }
 }
};
}

int RunScrollLayoutTests(){
 int failed=0;
 for(float dpi:{1.f,1.25f})for(ImVec2 size:{ImVec2{1240,820},ImVec2{880,600}}){
  ScrollFixture fixture(size,dpi);fixture.Run();failed+=fixture.failed;
 }
 // Windows may clamp the minimum requested height to a shorter work area.
 // Exercise Home there without imposing Library pane-height assumptions.
 {ScrollFixture fixture({880,480},1.5f);fixture.HeaderControls();fixture.Home();failed+=fixture.failed;}
 // The requested physical 1280x680 work area at 150% leaves 853x453 logical
 // pixels. Keep this Library-only so its compact-header contract is explicit.
 {ScrollFixture fixture({1280.f/1.5f,680.f/1.5f},1.5f);fixture.HeaderControls();fixture.Library(true);failed+=fixture.failed;}
 return failed;
}
