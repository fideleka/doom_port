#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <cstdarg>
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
using EventGroupHandle_t = void*;
constexpr int pdTRUE=1, pdPASS=1, portMAX_DELAY=10000;
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4;
constexpr int DOOMGENERIC_RESX=320, DOOMGENERIC_RESY=240, FONT_6x12=1;
#define pdMS_TO_TICKS(x) (x)
struct Halt {};
int failAt=0, operation=0, taskCalls=0, engineCalls=0, ticks=0, waits=0;
int frameEvents=0, lockDepth=0;
bool frameUiMode=false, frameWipeActive=false, inhelpscreens=false;
int gamestate=0; constexpr int GS_LEVEL=1;
bool D_WipeInProgress(){return false;}
int myargc; char** myargv;
bool bootConsoleActive=true;
TaskHandle_t gameTaskHandle=nullptr, drawTaskHandle=nullptr;
SemaphoreHandle_t inputMutex=nullptr, backBufferMutex=nullptr;
EventGroupHandle_t backBufferEvent=nullptr;
uint16_t* backBuffer=nullptr; uint16_t* DG_ScreenBuffer=nullptr;
void xSemaphoreTake(void* p,int){assert(p==backBufferMutex);++lockDepth;}
void xSemaphoreGive(void* p){assert(p==backBufferMutex&&lockDepth==1);--lockDepth;}
void xEventGroupSetBits(void* p,int bits){assert(p==backBufferEvent&&bits==1&&lockDepth==1);++frameEvents;}
std::set<void*> live;
std::vector<std::string> logs, visible;
std::vector<TaskHandle_t> notifications;
void* trackedAlloc(size_t n) {
    if (++operation==failAt) return nullptr;
    void* p=std::malloc(n); assert(p); live.insert(p); return p;
}
void trackedFree(void* p) {
    if (!p) return;
    assert(live.erase(p)==1); std::free(p);
}
#define free trackedFree
#define malloc trackedAlloc
void* ps_malloc(size_t n) { return trackedAlloc(n); }
void* xSemaphoreCreateMutex(){return trackedAlloc(1);}
void* xEventGroupCreate(){return trackedAlloc(1);}
void vSemaphoreDelete(void* p){trackedFree(p);}
void vEventGroupDelete(void* p){trackedFree(p);}
size_t heap_caps_get_free_size(int){return 100000;}
size_t heap_caps_get_largest_free_block(int){return 50000;}
void buttonHandler(int,bool){}
void restartAfterDoomQuit(){}
void gameTask(void*);
void drawTask(void*);
int xTaskCreatePinnedToCore(void(*fn)(void*), const char* name, uint32_t stack, void*, int priority, void** handle, int core){
    ++taskCalls; assert(priority==1);
    if(fn==drawTask){assert(std::strcmp(name,"drawTask")==0&&stack==16384&&core==1);}
    else {assert(fn==gameTask&&std::strcmp(name,"gameTask")==0&&stack==32768&&core==0);}
    *handle=trackedAlloc(1); return *handle?pdPASS:0;
}
void vTaskDelete(void* p){assert(notifications.empty());trackedFree(p);}
int ulTaskNotifyTake(int clear, int timeout){assert(clear==pdTRUE&&timeout==portMAX_DELAY);++waits;return 1;}
void xTaskNotifyGive(void* p){assert(p&&engineCalls==1&&taskCalls==2);notifications.push_back(p);}
void vTaskDelay(int){throw Halt{};}
namespace lilka {
namespace colors {constexpr int Black=0,White=1;}
void serial_log(const char* fmt,...){char line[256];va_list args;va_start(args,fmt);vsnprintf(line,sizeof(line),fmt,args);va_end(args);logs.push_back(line);}
struct Controller{void setGlobalHandler(void(*)(int,bool)){assert(gameTaskHandle&&drawTaskHandle&&engineCalls==1);}} controller;
struct Display{
 void fillScreen(int){}void setFont(int){}void setTextColor(int){}void setCursor(int,int){}
 void print(const char* s){visible.push_back(s);}
} display;
}
void DG_printf(const char*,...){}
void I_AtExit(void(*)(),bool){}
void DG_Init(){assert(backBuffer&&DG_ScreenBuffer);}
void M_FindResponseFile(){}
void I_Error(char const*){throw Halt{};}
extern "C" void DG_DrawFrame();
void D_DoomMain(){assert(backBuffer&&DG_ScreenBuffer&&gameTaskHandle&&drawTaskHandle&&notifications.empty());
 auto* oldBack=backBuffer;auto* oldScreen=DG_ScreenBuffer;DG_DrawFrame();
 assert(backBuffer==oldScreen&&DG_ScreenBuffer==oldBack&&frameUiMode&&!frameWipeActive&&lockDepth==0);++engineCalls;}
void* engineBuffers[4]={};
int allocEngine(int i){engineBuffers[i]=trackedAlloc(16);return engineBuffers[i]?16:-1;}
int R_AllocMain(){return allocEngine(0);}int R_AllocPlanes(){return allocEngine(1);}
int R_AllocThings(){return allocEngine(2);}int R_AllocBSP(){return allocEngine(3);}
void D_FreeBuffers(){for(auto& p:engineBuffers){trackedFree(p);p=nullptr;}}
#include "production.inc"
void gameTask(void*){waitForEngineStart();++ticks;}
void drawTask(void*){waitForEngineStart();}
int main(){
 static_assert(gameStackBytes==32768,"engine stack preserved");
 static_assert(drawStackBytes==16384,"bounded renderer stack");
 // 3 synchronization objects, 2 framebuffers, 2 tasks, 4 engine buffers.
 for(int failure=1;failure<=11;++failure){
  failAt=failure;operation=taskCalls=engineCalls=ticks=waits=0;logs.clear();visible.clear();notifications.clear();bootConsoleActive=true;
  try{initializeDoomRuntime(0,nullptr);assert(false);}catch(Halt&){}
  assert(engineCalls==0&&ticks==0&&notifications.empty()&&live.empty());
  assert(!gameTaskHandle&&!drawTaskHandle&&!inputMutex&&!backBufferMutex&&!backBufferEvent&&!backBuffer&&!DG_ScreenBuffer);
  assert(visible.size()==3&&visible[0]=="DOOM STARTUP FAILED");
  assert(!bootConsoleActive&&logs.back().find("FAILED")!=std::string::npos);
 }
 failAt=0;operation=taskCalls=engineCalls=0;visible.clear();logs.clear();notifications.clear();
 initializeDoomRuntime(0,nullptr);
 assert(operation==11&&engineCalls==1&&notifications.size()==2&&visible.empty()&&frameEvents==1);
 assert(logs.empty()); // Successful startup has no temporary heap/task diagnostics.
 assert(notifications[0]==drawTaskHandle&&notifications[1]==gameTaskHandle);
 assert(!bootConsoleActive);gameTask(nullptr);drawTask(nullptr);assert(waits==2&&ticks==1);
 notifications.clear();D_FreeBuffers();releaseStartupResources();assert(live.empty());
 // Generic host callers still get a checked fallback allocation; failure
 // cannot reach DG_Init or D_DoomMain with a null framebuffer.
 failAt=1;operation=engineCalls=0;
 try{doomgeneric_Create(0,nullptr);assert(false);}catch(Halt&){}
 assert(engineCalls==0&&live.empty());
 std::puts("startup: all 11 allocation failures, task gating, cleanup, startup frame swap, success and framebuffer fallback PASS");
}
