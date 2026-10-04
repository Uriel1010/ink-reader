#include <Arduino.h>
#include <memory>
#include <new>
#include <vector>
#include <esp_heap_caps.h>
#include "PNGdec.h"
extern "C" {
#include "tjpgd.h"
}
#include "BookImages.h"
#include "StbDecoder.h"
static bool fallback(uint8_t *frame,uint8_t *data,size_t size,int x,int y,int w,int h,int cw,int ch);
struct ImageTarget {
  uint8_t *frame,*data; size_t size,pos;
  int x,y,w,h,sw,sh,canvasWidth,canvasHeight;
  std::shared_ptr<uint64_t> samples;
};
static void fit(ImageTarget &t,int sw,int sh) {
  t.sw=sw;t.sh=sh;
  int w=t.w,h=int(int64_t(sh)*w/sw);
  if(h>t.h){h=t.h;w=int(int64_t(sw)*h/sh);}
  t.x+=(t.w-w)/2;t.y+=(t.h-h)/2;t.w=std::max(1,w);t.h=std::max(1,h);
  if(size_t(t.w)*t.h<=160000)t.samples=std::shared_ptr<uint64_t>((uint64_t*)heap_caps_calloc(size_t(t.w)*t.h,sizeof(uint64_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),free);
}
static void pixel(ImageTarget &t,int sx,int sy,int lum) {
  if(!t.samples||sx<0||sy<0||sx>=t.sw||sy>=t.sh)return;
  // Every source sample contributes: downsampling averages fine details instead of discarding them.
  int x0=sx*t.w/t.sw,x1=std::max(x0+1,(sx+1)*t.w/t.sw);
  int y0=sy*t.h/t.sh,y1=std::max(y0+1,(sy+1)*t.h/t.sh);
  for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
    if(xx<t.w&&yy<t.h)t.samples.get()[yy*t.w+xx]+=(uint64_t(1)<<32)+lum;
  }
}
static bool finish(ImageTarget &t){
  if(!t.samples)return false;
  std::vector<int> current(t.w+2,0),next(t.w+2,0);
  for(int yy=0;yy<t.h;yy++){
    bool reverse=yy&1;int step=reverse?-1:1;
    for(int xx=reverse?t.w-1:0;xx>=0&&xx<t.w;xx+=step){
      uint64_t sample=t.samples.get()[yy*t.w+xx];uint32_t count=sample>>32;
      int lum=count?int(uint32_t(sample)/count):255;
      int value=std::max(0,std::min(255*16,lum*16+current[xx+1]));
      bool white=value>=128*16;int error=value-(white?255*16:0);
      current[xx+1+step]+=error*7/16;next[xx+1-step]+=error*3/16;next[xx+1]+=error*5/16;next[xx+1+step]+=error/16;
    int x=t.x+xx,y=t.y+yy;if(x<0||x>=t.canvasWidth||y<0||y>=t.canvasHeight)continue;
    uint8_t mask=0x80>>(x&7);uint8_t &b=t.frame[y*((t.canvasWidth+7)/8)+x/8];
      if(white)b|=mask;else b&=~mask;
    }
    current.swap(next);std::fill(next.begin(),next.end(),0);
  }
  return true;
}
struct PngTarget {ImageTarget target;PNG *decoder;uint16_t row[2048];};
static int pngDraw(PNGDRAW *line) {
  PngTarget &p=*(PngTarget *)line->pUser;
  if(line->iWidth>2048)return 0;
  p.decoder->getLineAsRGB565(line,p.row,PNG_RGB565_LITTLE_ENDIAN,0xFFFFFF);
  for(int x=0;x<line->iWidth;x++){uint16_t c=p.row[x];int r=((c>>11)&31)*255/31,g=((c>>5)&63)*255/63,b=(c&31)*255/31;pixel(p.target,x,line->y,(r*77+g*150+b*29)>>8);}
  return 1;
}
static size_t jpegRead(JDEC *d,uint8_t *buffer,size_t count){
  ImageTarget &t=*(ImageTarget *)d->device;count=std::min(count,t.size-t.pos);
  if(buffer)memcpy(buffer,t.data+t.pos,count);t.pos+=count;return count;
}
static int jpegDraw(JDEC *d,void *bitmap,JRECT *r){
  ImageTarget &t=*(ImageTarget *)d->device;uint8_t *p=(uint8_t *)bitmap;
  for(int y=r->top;y<=r->bottom;y++)for(int x=r->left;x<=r->right;x++,p+=3)pixel(t,x,y,(p[0]*77+p[1]*150+p[2]*29)>>8);
  return 1;
}
bool renderBookImage(uint8_t *frame,uint8_t *data,size_t size,int x,int y,int w,int h,int canvasWidth,int canvasHeight){
  if(!data||size<8)return false;
  ImageTarget t{frame,data,size,0,x,y,w,h,0,0,canvasWidth,canvasHeight};int result=-1;
  if(!memcmp(data,"\x89PNG\r\n\x1a\n",8)){
    std::unique_ptr<PNG> png(new(std::nothrow) PNG);
    std::unique_ptr<PngTarget> p(new(std::nothrow) PngTarget);
    if(!png||!p)return false;
    result=png->openRAM(data,size,pngDraw);
    if(result==PNG_SUCCESS&&png->getWidth()>0&&png->getWidth()<=2048&&png->getHeight()>0&&png->getHeight()<=8192&&uint64_t(png->getWidth())*png->getHeight()<=5000000){
      p->target=t;p->decoder=png.get();fit(p->target,png->getWidth(),png->getHeight());result=p->target.samples?png->decode(p.get(),PNG_CHECK_CRC):PNG_TOO_BIG;
    }else result=PNG_TOO_BIG;
    png->close();
    Serial.printf("IMAGE PNG bytes=%u result=%d\n",unsigned(size),result);if(result==PNG_SUCCESS)return finish(p->target);p.reset();return fallback(frame,data,size,x,y,w,h,canvasWidth,canvasHeight);
  }
  if(data[0]==0xFF&&data[1]==0xD8){
    std::unique_ptr<uint8_t[]> pool(new(std::nothrow) uint8_t[32768]);if(!pool)return false;
    JDEC d{};result=jd_prepare(&d,jpegRead,pool.get(),32768,&t);
    if(result==JDR_OK&&uint64_t(d.width)*d.height>5000000)result=JDR_MEM1;
    if(result==JDR_OK){
      uint8_t scale=0;while(scale<3&&(d.width>>(scale+1))>=w&&(d.height>>(scale+1))>=h)++scale;
      fit(t,d.width,d.height);t.sw=d.width>>scale;t.sh=d.height>>scale;result=t.samples?jd_decomp(&d,jpegDraw,scale):JDR_MEM1;
    }
    Serial.printf("IMAGE JPEG bytes=%u result=%d\n",unsigned(size),result);if(result==JDR_OK)return finish(t);t.samples.reset();return fallback(frame,data,size,x,y,w,h,canvasWidth,canvasHeight);
  }
  Serial.println("IMAGE unsupported format");return false;
}
static bool fallback(uint8_t *frame,uint8_t *data,size_t size,int x,int y,int w,int h,int cw,int ch){
  int sw=0,sh=0;uint8_t *pixels=decodeImageFallback(data,size,sw,sh);
  if(!pixels){Serial.println("IMAGE fallback failed or memory limit");return false;}
  ImageTarget t{frame,data,size,0,x,y,w,h,0,0,cw,ch};fit(t,sw,sh);
  int channels=data[0]==0xFF?1:2;
  if(!t.samples){freeImageFallback(pixels);return false;}
  for(int yy=0;yy<sh;yy++)for(int xx=0;xx<sw;xx++){
    size_t offset=(size_t(yy)*sw+xx)*channels;
    int a=channels==2?pixels[offset+1]:255,lum=(pixels[offset]*a+255*(255-a)+127)/255;pixel(t,xx,yy,lum);
  }
  freeImageFallback(pixels);Serial.printf("IMAGE fallback decoded=%dx%d\n",sw,sh);return finish(t);
}
bool imageQualitySelfTest(){
  uint8_t frame[512];memset(frame,255,sizeof(frame));
  ImageTarget t{frame,nullptr,0,0,0,0,64,64,0,0,64,64};fit(t,128,128);
  for(int y=0;y<128;y++)for(int x=0;x<128;x++)pixel(t,x,y,(x+y)&1?255:0);
  bool ok=finish(t);int black=0;for(uint8_t b:frame)black+=8-__builtin_popcount(unsigned(b));
  ok=ok&&black>1800&&black<2300;
  memset(frame,255,sizeof(frame));t.samples.reset();t.x=t.y=0;t.w=t.h=64;fit(t,64,64);
  for(int y=0;y<64;y++)for(int x=0;x<64;x++)pixel(t,x,y,x<32?0:255);
  ok=finish(t)&&ok;for(int y=0;y<64;y++)for(int x=0;x<8;x++)ok=ok&&frame[y*8+x]==(x<4?0:255);
  Serial.printf("IMAGE_QUALITY average_downsample/neutral_gray/lossless_edges pass=%d black=%d\n",ok,black);return ok;
}
