#include <Arduino.h>
#include <esp_heap_caps.h>
#include "ReaderSmooth.h"
#include "ReaderGrayProbe.h"
#include "miniz.h"
static uint8_t *overlay=nullptr,*physical=nullptr;
static bool active=false,valid=false;static int width=400,height=300;
static uint32_t lastCRC=0,pendingCRC=0;
bool readerSmoothBegin(bool enabled,int w,int h){
  active=false;if(!enabled)return false;
  if(!overlay)overlay=(uint8_t *)heap_caps_malloc(30000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!physical)physical=(uint8_t *)heap_caps_malloc(30000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!overlay||!physical)return false;
  if(w*h!=120000)return false;memset(overlay,0,30000);width=w;height=h;active=true;return true;
}
bool readerSmoothActive(){return active;}
void readerSmoothPixel(int x,int y,uint8_t shade){
  if(!active||x<0||y<0||x>=width||y>=height||shade==3)return;
  // Overlay 1=gray, 2=black, 0=untouched; gray controller code 01 is forbidden.
  size_t p=size_t(y)*width+x;int shift=6-2*(p%4);uint8_t ink=shade==0?2:1;
  uint8_t old=(overlay[p/4]>>shift)&3;if(old==2)return;
  overlay[p/4]=(overlay[p/4]&~(3<<shift))|(ink<<shift);
}
bool readerSmoothUnchanged(const uint8_t *mono,int rotation){
  if(!active)return false;
  for(int y=0;y<height;y++)for(int x=0;x<width;x++){
    int px=x,py=y;if(rotation==90){px=399-y;py=x;}else if(rotation==180){px=399-x;py=299-y;}else if(rotation==270){px=y;py=299-x;}
    size_t p=size_t(y)*width+x,q=size_t(py)*400+px;int shift=6-2*(q%4);
    uint8_t ink=(overlay[p/4]>>(6-2*(p%4)))&3;
    uint8_t shade=ink==1?2:ink==2?0:(mono[q/8]&(128>>(q%8)))?3:0;
    physical[q/4]=(physical[q/4]&~(3<<shift))|(shade<<shift);
  }
  pendingCRC=mz_crc32(0,physical,30000);return valid&&pendingCRC==lastCRC;
}
bool readerSmoothPresent(uint32_t &elapsed){
  if(!active)return false;
  bool ok=readerGrayFramePresent(physical,0,elapsed);valid=ok;if(ok)lastCRC=pendingCRC;return ok;
}
void readerSmoothResetDisplay(){valid=false;}
const uint8_t *readerSmoothFrame(){return physical;}
