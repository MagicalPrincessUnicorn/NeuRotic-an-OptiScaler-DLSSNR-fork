// Actual production GDI painting only; no App/COM/ImGui/D3D/provider/game startup.
#define NEUROTIC_STARTUP_FEEDBACK_TEST
#include "ui/StartupFeedback.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>

static int checks=0,failures=0;
static void Check(bool good,const char* label){++checks;failures+=!good;std::printf("%s %s\n",good?"PASS":"FAIL",label);}
static long long Area(RECT r){return static_cast<long long>(r.right-r.left)*(r.bottom-r.top);}
static bool Inside(RECT r,RECT client){return r.left>=client.left&&r.top>=client.top&&r.right<=client.right&&r.bottom<=client.bottom&&r.right>r.left&&r.bottom>r.top;}
static bool Aspect(RECT r,int width,int height){return std::llabs(static_cast<long long>(r.right-r.left)*height-static_cast<long long>(r.bottom-r.top)*width)<=(std::max)(width,height);}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
 struct Case{const char* name;UINT dpi;RECT work;};
 const Case cases[]={{"portrait-96",96,{0,0,1920,1080}},{"portrait-144",144,{0,0,1920,1080}},
     {"portrait-192",192,{0,0,2560,1440}},{"fit-small-192",192,{0,0,640,480}},
     {"negative-origin-288",288,{-1600,-900,-320,60}}};
 for(const auto& test:cases){
  const RECT fit=nh::StartupFeedback::FitForTest(test.work,test.dpi);const int width=fit.right-fit.left,height=fit.bottom-fit.top;
  Check(Inside(fit,test.work)&&width<height,"portrait stays inside its actual work area at requested DPI");
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;info.bmiHeader.biSizeImage=static_cast<DWORD>(width*height*4);
  HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  if(!dc||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);Check(false,"owned GDI capture surface is available");continue;}
  const auto previous=SelectObject(dc,bitmap);nh::StartupFeedback::Receipt receipt;const RECT client{0,0,width,height};
  const bool drawn=nh::StartupFeedback::PaintForTest(dc,client,test.dpi,L"Loading...",receipt);
  // CreateDIBSection requires completed GDI drawing before any direct bits access.
  const bool flushed=GdiFlush()!=FALSE;Check(flushed,"GDI drawing is synchronized before accessing captured pixels");
  if(!flushed){SelectObject(dc,previous);const bool dcReleased=DeleteDC(dc)!=FALSE;const bool bitmapReleased=DeleteObject(bitmap)!=FALSE;Check(dcReleased&&bitmapReleased,"failed capture releases its owned bitmap/DC without reading pixels");continue;}
  Check(drawn&&receipt.painted&&receipt.brandLoaded&&receipt.gdiReleased,"production GDI paints embedded official artwork and releases its owned font/icon");
  Check(Inside(receipt.mark,client)&&Inside(receipt.wordmark,client)&&Inside(receipt.status,client)&&
      receipt.mark.bottom<receipt.wordmark.top&&receipt.wordmark.bottom<receipt.status.top,
      "official N, genuine wordmark and loading caption are vertically ordered and contained");
  Check(Area(receipt.mark)>Area(receipt.wordmark)&&Area(receipt.mark)>Area(receipt.status),"official N is the largest visual element");
  Check(Aspect(receipt.mark,nh::startup_brand::MarkWidth,nh::startup_brand::MarkHeight)&&
      Aspect(receipt.wordmark,nh::startup_brand::WordmarkWidth,nh::startup_brand::WordmarkHeight),"both official artwork source aspects survive native fitting");
  BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+info.bmiHeader.biSizeImage;
  std::ofstream file(output/(std::string(test.name)+".bmp"),std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<const char*>(pixels),info.bmiHeader.biSizeImage);file.close();
  Check(bool(file),"actual native GDI pixels are preserved before surface cleanup");
  std::printf("PORTRAIT_CASE {\"name\":\"%s\",\"dpi\":%u,\"window\":[%ld,%ld,%ld,%ld],\"client\":[%d,%d],\"mark\":[%ld,%ld,%ld,%ld],\"wordmark\":[%ld,%ld,%ld,%ld],\"status\":[%ld,%ld,%ld,%ld]}\n",
      test.name,test.dpi,fit.left,fit.top,fit.right,fit.bottom,width,height,receipt.mark.left,receipt.mark.top,receipt.mark.right,receipt.mark.bottom,
      receipt.wordmark.left,receipt.wordmark.top,receipt.wordmark.right,receipt.wordmark.bottom,receipt.status.left,receipt.status.top,receipt.status.right,receipt.status.bottom);
  SelectObject(dc,previous);const bool dcReleased=DeleteDC(dc)!=FALSE;const bool bitmapReleased=DeleteObject(bitmap)!=FALSE;Check(dcReleased&&bitmapReleased,"owned capture bitmap/DC are released");
 }
 std::printf("RESULT checks=%d failures=%d ACTUAL_PRODUCTION_GDI=true NO_APP_OR_GRAPHICS_DEVICE=true\n",checks,failures);return failures?1:0;
}
