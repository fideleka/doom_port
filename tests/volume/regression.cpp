#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <lilka/volume_overlay.h>
#include "doomkeys.h"
constexpr int DOOMGENERIC_RESX=320, SCREENHEIGHT_UI=200;
uint32_t clockNow=100;
uint32_t millis(){return clockNow;}
constexpr int pdTRUE=1,portMAX_DELAY=10000;
#define pdMS_TO_TICKS(x) (x)
int inputMutex=1,backBufferMutex=2,backBufferEvent=3,lockDepth=0;
void xSemaphoreTake(int,int){++lockDepth;}
void xSemaphoreGive(int){assert(lockDepth>0);--lockDepth;}
void taskYIELD(){}
struct Stop{};
std::vector<int> events;
size_t eventIndex=0;
bool verifyPixels=false;
std::vector<uint16_t> basePixels,expected;
std::vector<unsigned> hits;
namespace lilka {
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
}
struct doomkey_t {int key;bool pressed;};
doomkey_t keyqueue[16];int keyqueueWrite=0;char nextWeaponKey='2';
#ifndef KEY_FIRE
#define KEY_FIRE KEY_RCTRL
#define KEY_USE ' '
#endif
uint16_t buffer[320*240];uint16_t* backBuffer=buffer;
bool frameUiMode=false,frameWipeActive=false;
uint16_t ST_HudBackground565(int x,int y){return static_cast<uint16_t>((x*31+y*7)&65535);}
void validatePrevious(){
 if(eventIndex==0)return;
 assert(lockDepth==0&&lilka::display.depth==0);
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
  lilka::drawVolumeOverlay(reference,lilka::audio.snapshot,reference.w,reference.h,clockNow);
  expected=reference.pixels;
 }
 hits.assign(lilka::display.w*lilka::display.h,0);
 return events[eventIndex++];
}
#include "production.inc"
void run(){eventIndex=0;try{drawTask(nullptr);}catch(Stop&){}assert(lockDepth==0);}
int main(){
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
  lilka::audio.snapshot.valid=false;events={1};verifyPixels=false;run();basePixels=lilka::display.pixels;
  for(int level:{0,1,4,5,50,100}){
   lilka::display.pixels=basePixels;
   lilka::audio.snapshot.valid=true;lilka::audio.snapshot.level=level;lilka::audio.snapshot.adjustedAt=100;
   events={1,0,0,0};verifyPixels=true;run();assert(lilka::display.pixels==basePixels);
   assert(std::equal(immutable.begin(),immutable.end(),buffer));
  }
 }
 puts("Actual Doom Select ignored/Start mapping + final-only regular-font rows, UI/world/wipe/HUD, orientations, paused expiry, immutable sources PASS");
}
