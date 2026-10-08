#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <lilka/volume_overlay.h>
#include "doomkeys.h"
constexpr int DOOMGENERIC_RESX=320, SCREENHEIGHT_UI=200, DOOMGENERIC_RESY=240;
uint32_t clockNow=100;
uint32_t millis(){return clockNow;}
constexpr int pdTRUE=1,portMAX_DELAY=10000;
#define pdMS_TO_TICKS(x) (x)
int inputMutex=1,backBufferMutex=2,backBufferEvent=3,lockDepth=0;
void xSemaphoreTake(int,int){++lockDepth;}
void xSemaphoreGive(int){assert(lockDepth>0);--lockDepth;}
void taskYIELD(){}
void waitForEngineStart(){}
struct Stop{};
std::vector<int> events;
size_t eventIndex=0;
bool verifyPixels=false;
std::vector<uint16_t> basePixels,expected;
std::vector<unsigned> hits;
namespace lilka {
void serial_log(const char*){}
enum class Button {UP,DOWN,LEFT,RIGHT,A,B,C,D,SELECT,START};
namespace colors{constexpr uint16_t Black=0;}
struct Surface {
 int w=280,h=240;
 std::vector<uint16_t> pixels;
 void reset(int width,int height){w=width;h=height;pixels.assign(w*h,0);}
 void fillRect(int x,int y,int width,int height,uint16_t c){
  assert(x>=0&&y>=0&&x+width<=w&&y+height<=h);
  for(int j=y;j<y+height;j++)for(int i=x;i<x+width;i++)pixels[j*w+i]=c;
 }
 void drawRect(int x,int y,int width,int height,uint16_t c){fillRect(x,y,width,1,c);fillRect(x,y+height-1,width,1,c);fillRect(x,y,1,height,c);fillRect(x+width-1,y,1,height,c);}
};
struct Display:Surface {
 int depth=0,wx=0,wy=0,ww=0,wh=0,cursor=0;
 int width(){return w;}int height(){return h;}
 void startWrite(){assert(depth++==0);}
 void endWrite(){assert(--depth==0);}
 void writeAddrWindow(int x,int y,int width,int height){assert(depth==1&&x>=0&&y>=0&&x+width<=w&&y+height<=h);wx=x;wy=y;ww=width;wh=height;cursor=0;}
 void writePixels(uint16_t* p,int count){assert(depth==1&&cursor+count<=ww*wh);for(int i=0;i<count;i++){int pos=(wy+cursor/ww)*w+wx+cursor%ww; if(verifyPixels)assert(p[i]==expected[pos]); assert(++hits[pos]==1);pixels[pos]=p[i];cursor++;}}
} display;
struct Audio {VolumeOverlaySnapshot snapshot;VolumeOverlaySnapshot getVolumeOverlay(){return snapshot;}} audio;
struct Brightness {VolumeOverlaySnapshot snapshot;VolumeOverlaySnapshot getOverlay(){return snapshot;}} brightness;
VolumeOverlaySnapshot expectedOverlay(uint32_t now) {
 const auto sound=audio.snapshot,light=brightness.snapshot;
 if(light.visible(now)&&(!sound.visible(now)||now-light.adjustedAt<now-sound.adjustedAt))return light;
 return sound;
}
}
struct doomkey_t {int key;bool pressed;};
doomkey_t keyqueue[16];int keyqueueWrite=0;char nextWeaponKey='2';
#ifndef KEY_FIRE
#define KEY_FIRE KEY_RCTRL
#define KEY_USE ' '
#endif
uint16_t buffer[320*240];uint16_t* backBuffer=buffer;
bool frameUiMode=false,frameWipeActive=false;
bool engineWipe=false, inhelpscreens=false;
constexpr int GS_LEVEL=0;
int gamestate=GS_LEVEL;
uint16_t engineRGB[320*240];
uint16_t* DG_ScreenBuffer=engineRGB;
bool D_WipeInProgress(){return engineWipe;}
void xEventGroupSetBits(int,int){}
uint8_t ST_HudBackgroundIndex(int x,int y){return static_cast<uint8_t>((x*31+y*7)&255);}
std::vector<int> randomTrace;
extern "C" {
uint8_t indexedBuffer[320*240];
uint8_t* I_VideoBuffer=indexedBuffer;
void* Z_Malloc(int size,int,void*){return std::malloc(size);}
void Z_Free(void* p){std::free(p);}
int M_Random(){static unsigned n=1; n=n*1664525+1013904223;int v=(n>>16)&255;randomTrace.push_back(v);return v;}
void I_ReadScreen(uint8_t* p){std::memcpy(p,I_VideoBuffer,320*240);}
void V_MarkRect(int x,int y,int w,int h){assert(x==0&&y==0&&w<=320&&h<=240);}
int wipe_StartScreen(int,int,int,int);
int wipe_EndScreen(int,int,int,int);
int wipe_ScreenWipe(int,int,int,int,int,int);
}
bool identityPalette=false;
uint16_t ST_HudBackground565(int x,int y){return static_cast<uint16_t>((x*31+y*7)&(identityPalette?255:65535));}
void validatePrevious(){
 if(eventIndex==0)return;
 assert(lockDepth==0&&lilka::display.depth==0);
 if(verifyPixels && eventIndex==1 && !lilka::audio.snapshot.valid && lilka::brightness.snapshot.level==50) {
  if(const char* directory=std::getenv("DOOM_OVERLAY_CAPTURE")) {
   char path[512];std::snprintf(path,sizeof(path),"%s/doom-brightness-%d.ppm",directory,lilka::display.w);
   FILE* file=std::fopen(path,"wb");assert(file);
   std::fprintf(file,"P6\n%d %d\n255\n",lilka::display.w,lilka::display.h);
   for(auto p:lilka::display.pixels){unsigned char rgb[]={static_cast<unsigned char>((p>>11)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};std::fwrite(rgb,1,3,file);}
   std::fclose(file);
  }
 }
 if(verifyPixels){
  bool wrote=std::any_of(hits.begin(),hits.end(),[](unsigned n){return n!=0;});
  assert(wrote == (eventIndex <= 3)); // Appearance, paused refresh and expiry must present.
  if(wrote){for(auto n:hits)assert(n==1);assert(lilka::display.pixels==expected);}
 }
}
int xEventGroupWaitBits(int,int,int,int,int timeout){
 assert(timeout==50);validatePrevious();
 if(eventIndex>=events.size())throw Stop();
 clockNow=eventIndex==0?100:eventIndex==1?500:eventIndex==2?1300:1400;
 if(verifyPixels){
  lilka::Surface reference;reference.reset(lilka::display.w,lilka::display.h);reference.pixels=basePixels;
  lilka::drawVolumeOverlay(reference,lilka::expectedOverlay(clockNow),reference.w,reference.h,clockNow);
  expected=reference.pixels;
 }
 hits.assign(lilka::display.w*lilka::display.h,0);
 return events[eventIndex++];
}
#include "production.inc"
void run(){eventIndex=0;try{drawTask(nullptr);}catch(Stop&){}assert(lockDepth==0);}
// Independent accepted-layout oracle: retained native crop, face/ARMS swap,
// ammo cell rows/dividers/endcaps, UI letterboxing, and in-level menu framing.
void classicLayoutCheck(bool uiMode){
 const int w=lilka::display.w,h=lilka::display.h;
 const int ah=10*w/280,sh=32*w/280,world=h-ah-sh,sw=251*w/280,sx=(w-sw)/2;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  uint16_t pixel=0;
  if(uiMode){const int uh=w*3/4,uy=(h-uh)/2;if(y>=uy&&y<uy+uh)pixel=backBuffer[((y-uy)*200/uh)*320+x*320/w];}
  else if(y<world)pixel=backBuffer[(y*208/world)*320+20+x*280/w];
  else if(y<world+ah){
   const int ay=y-world,type=x*4/w,cw=w/4,in=x-type*w/4,tx=250+in*70/cw;
   if(ay<2||ay>=ah-2)pixel=ST_HudBackground565(tx,ay<2?ay:32-(ah-ay));
   else if(in<2||(type==3&&in>=cw-2))pixel=ST_HudBackground565(249+in%2,ay*31/(ah-1));
   else pixel=backBuffer[(213+type*6+(ay-2)*6/(ah-4))*320+tx];
  }else{
   const int sy=(y-world-ah)*32/sh;
   if(x<sx)pixel=ST_HudBackground565(305+x*14/sx,sy);
   else if(x>=sx+sw)pixel=ST_HudBackground565(305+(x-sx-sw)*15/(w-sx-sw),sy);
   else{int tx=(x-sx)*251/sw;if(tx>=104&&tx<139)tx+=39;else if(tx>=139&&tx<178)tx-=35;pixel=backBuffer[(208+sy)*320+tx];}
  }
  assert(lilka::display.pixels[y*w+x]==pixel);
 }
}
// Real engine melt + actual final LCD writes. Synthetic indexed art only.
void transitionTests(){
 identityPalette=true;
 for(int orientation=0;orientation<2;++orientation)for(int from=0;from<2;++from)for(int to=0;to<2;++to){
  lilka::display.reset(orientation?240:280,orientation?280:240);
  const DoomPresentation geometry(lilka::display.w,lilka::display.h);
  std::vector<uint8_t> oldSource(320*240),newSource(320*240),start(320*240),end(320*240);
  for(int y=0;y<240;++y)for(int x=0;x<320;++x){
   oldSource[y*320+x]=static_cast<uint8_t>((y*3+x*7+19)&255);
   newSource[y*320+x]=static_cast<uint8_t>((y*11+x*13+101)&255);
  }
  verifyPixels=false;engineWipe=false;lilka::audio.snapshot.valid=false;lilka::brightness.snapshot.valid=false;
  frameUiMode=from;frameWipeActive=false;
  for(int i=0;i<320*240;++i)backBuffer[i]=oldSource[i];
  events={1};run();const auto before=lilka::display.pixels;
  int width=0,height=0;
  DG_ComposeWipeFrame(oldSource.data(),start.data(),0,&width,&height);
  for(int i=0;i<width*height;++i)assert(before[i]==start[i]);
  std::memcpy(I_VideoBuffer,oldSource.data(),320*240);
  wipe_StartScreen(0,0,320,240);
  std::memcpy(I_VideoBuffer,newSource.data(),320*240);
  gamestate=to?1:GS_LEVEL;inhelpscreens=false;
  DG_ComposeWipeFrame(newSource.data(),end.data(),1,&width,&height);
  wipe_EndScreen(0,0,320,240);
  assert(std::equal(start.begin(),start.end(),I_VideoBuffer));
  bool done=false;int count=0;randomTrace.clear();
  std::vector<int> position(width,0);
  engineWipe=true;
  while(!done){
   done=wipe_ScreenWipe(1,0,0,320,240,1);assert(++count<100);
   if(count==1){
    assert(randomTrace.size()==static_cast<size_t>(width));
    position[0]=-(randomTrace[0]%16);
    for(int x=1;x<width;++x){position[x]=position[x-1]+randomTrace[x]%3-1;if(position[x]>0)position[x]=0;else if(position[x]==-16)position[x]=-15;}
   }
   // Independent melt trajectory checks every pixel, including moving HUD
   // background/endcaps. No static HUD can float over uncomposited columns.
   for(int pair=0;pair<width/2;++pair){
    int& offset=position[pair];
    if(offset<0)++offset;
    else if(offset<height)offset=std::min(height,offset+(offset<16?offset+1:8));
    const int split=std::max(0,offset);
    for(int y=0;y<height;++y)for(int dx=0;dx<2;++dx){
     const int x=pair*2+dx;
     const auto pixel=y<split?end[y*width+x]:start[(y-split)*width+x];
     assert(I_VideoBuffer[y*width+x]==pixel);
    }
   }
   for(int i=0;i<320*240;++i)DG_ScreenBuffer[i]=I_VideoBuffer[i];
   DG_DrawFrame();assert(frameWipeActive);
   verifyPixels=false;lilka::audio.snapshot.valid=false;lilka::brightness.snapshot.valid=false;events={1};run();
   for(int i=0;i<width*height;++i)assert(lilka::display.pixels[i]==I_VideoBuffer[i]);
   // Overlay must be applied once, only to final physical rows, never to the
   // indexed wipe or either endpoint, including transitions OUT of a level.
   basePixels=lilka::display.pixels;
   lilka::audio.snapshot.valid=true;lilka::audio.snapshot.level=50;lilka::audio.snapshot.adjustedAt=100;
   events={1,0,0,0};verifyPixels=true;run();
   assert(lilka::display.pixels==basePixels);
  }
  assert(std::equal(end.begin(),end.begin()+width*height,I_VideoBuffer));
  const auto finalWipe=lilka::display.pixels;
  // A second transition without an intervening native redraw must retain
  // the physical endpoint verbatim rather than composing it a second time.
  std::vector<uint8_t> consecutive(320*240);
  DG_ComposeWipeFrame(I_VideoBuffer,consecutive.data(),0,&width,&height);
  assert(std::equal(consecutive.begin(),consecutive.end(),I_VideoBuffer));
  engineWipe=false;
  // Simulate the engine's forced native redraw at the next frame.
  for(int i=0;i<320*240;++i)DG_ScreenBuffer[i]=newSource[i];
  DG_DrawFrame();assert(!frameWipeActive);
  verifyPixels=false;lilka::audio.snapshot.valid=false;lilka::brightness.snapshot.valid=false;events={1};run();
  assert(lilka::display.pixels==finalWipe);
  // Independently compose the native endpoint for exact full-panel equality.
  std::vector<uint8_t> nativePhysical(width*height);
  for(int y=0;y<height;++y)geometry.row(newSource.data(),to,y,nativePhysical.data()+y*width,ST_HudBackgroundIndex);
  for(int i=0;i<width*height;++i)assert(finalWipe[i]==nativePhysical[i]);
  // World top/bottom and the HUD boundary cannot change across final melt.
  if(!to){
   const int worldHeight=height-geometry.ammoHeight-geometry.statusHeight;
   for(int y:{0,worldHeight/2,worldHeight-1})for(int x:{0,width/2,width-1}){
    const int sourceY=y*208/worldHeight,sourceX=20+x*280/width;
    assert(finalWipe[y*width+x]==newSource[sourceY*320+sourceX]);
    assert(lilka::display.pixels[y*width+x]==finalWipe[y*width+x]);
   }
  }
 }
 puts("Production indexed physical melt + actual LCD writes: UI/level entry/exit, rotations, endpoints, stable world geometry, final-only overlay PASS");
}
int main(){
 printf("DoomPresentation geometry host bytes: %zu\n",sizeof(DoomPresentation));
 for(bool p:{false,true}){buttonHandler(lilka::Button::SELECT,p);assert(keyqueueWrite==0);}
 buttonHandler(lilka::Button::START,true);assert(keyqueue[0].key==KEY_ENTER&&keyqueue[0].pressed);
 buttonHandler(lilka::Button::START,false);assert(keyqueue[1].key==KEY_ENTER&&!keyqueue[1].pressed);
 buttonHandler(lilka::Button::UP,true);assert(keyqueue[2].key==KEY_UPARROW);
 buttonHandler(lilka::Button::DOWN,true);assert(keyqueue[3].key==KEY_DOWNARROW);
 for(int i=0;i<320*240;i++)buffer[i]=static_cast<uint16_t>((i*19+77)&65535);
 const std::vector<uint16_t> immutable(buffer,buffer+320*240);
 for(int orientation=0;orientation<2;orientation++)for(int mode=0;mode<3;mode++){
  lilka::display.reset(orientation?240:280,orientation?280:240);
  frameUiMode=mode==1;frameWipeActive=mode==2;
  lilka::audio.snapshot.valid=false;lilka::brightness.snapshot.valid=false;events={1};verifyPixels=false;run();if(mode!=2)classicLayoutCheck(frameUiMode);basePixels=lilka::display.pixels;
  for(int source=0;source<5;++source) for(int level:{0,1,4,5,50,100}){
   lilka::display.pixels=basePixels;
   lilka::audio.snapshot.valid=true;lilka::audio.snapshot.level=level;lilka::audio.snapshot.adjustedAt=100;
   lilka::brightness.snapshot.valid=source!=0;
   lilka::brightness.snapshot.brightness=true;
   lilka::brightness.snapshot.level=level;
   lilka::brightness.snapshot.adjustedAt=source==3?99:100;
   if(source==1)lilka::audio.snapshot.valid=false;
   if(source==2)lilka::audio.snapshot.adjustedAt=99;
   events={1,0,0,0};verifyPixels=true;run();assert(lilka::display.pixels==basePixels);
   assert(std::equal(immutable.begin(),immutable.end(),buffer));
  }
 }
 transitionTests();
 puts("Actual Doom Select ignored/Start mapping + final-only regular-font rows, UI/world/wipe/HUD, orientations, paused expiry, immutable sources PASS");
}
