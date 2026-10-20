from pathlib import Path
p=Path('OptiScaler/framegen/IFGFeature.h');s=p.read_text().replace('CanDispatchInputs(int index) const noexcept { return true; }','CanDispatchInputs(int) const noexcept { return true; }');p.write_text(s)
s=Path('OptiScaler/framegen/IFGFeature.cpp').read_text();a=s.index('int IFGFeature::GetDispatchIndex(');b=s.index('\n}',a)+2
source=r'''#include <cassert>
#include <cstdint>
#include <cstdio>
#define LOG_DEBUG(...)
using UINT64=uint64_t;
enum class FG_ResourceType{Depth,Velocity,UIColor,HudlessColor};
struct Option {unsigned value_or_default(){return 2;}};
struct Config {Option FGAllowedFrameAhead;static Config* Instance(){static Config c;return &c;}};
struct State {UINT64 fgLastFrame=99;static State& Instance(){static State s;return s;}};
struct IFGFeature {
 static constexpr unsigned BUFFER_COUNT=4;UINT64 _frameCount=9,_lastDispatchedFrame=8,_lastFGFrame=7;bool ready=false,resources=true;
 int GetIndex(){return int(_frameCount%BUFFER_COUNT);}bool HasResource(FG_ResourceType,int){return resources;}
 virtual bool CanDispatchInputs(int) const noexcept {return ready;}
 int GetDispatchIndex(UINT64&);
};
'''+s[a:b]+r'''
int main(){IFGFeature f;UINT64 frame=0;assert(f.GetDispatchIndex(frame)==-1&&f._lastDispatchedFrame==8&&f._lastFGFrame==7);
f.ready=true;assert(f.GetDispatchIndex(frame)==1&&frame==9&&f._lastDispatchedFrame==9&&f._lastFGFrame==99);assert(f.GetDispatchIndex(frame)==-1);
f._lastDispatchedFrame=3;f.ready=false;assert(f.GetDispatchIndex(frame)==-1&&f._lastDispatchedFrame==3);
f.ready=true;assert(f.GetDispatchIndex(frame)==1&&frame==9);puts("PASS production dispatch input gate preserves blocked frame for matching later constants");}
'''
Path('builds/audit-api/dispatch-gate.cpp').write_text(source)
