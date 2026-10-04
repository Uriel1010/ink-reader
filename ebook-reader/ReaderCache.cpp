#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <vector>
#include "cJSON.h"
#include "ReaderCache.h"
#include "BookImages.h"
#include "Epub.h"
uint32_t readerHash(const std::string &text){uint32_t h=2166136261u;for(uint8_t c:text){h^=c;h*=16777619u;}return h;}
uint32_t readerCRC(const uint8_t *data,size_t size){uint32_t crc=~0u;while(size--){crc^=*data++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xEDB88320u&-(crc&1));}return ~crc;}
static std::string cacheName(const std::string &key,const char *extension){char name[80];snprintf(name,sizeof(name),"/.crowreader/cache/%08lx%s",(unsigned long)readerHash(key),extension);return name;}
static uint32_t fileSignature(const std::string &path){
  File f=SD.open(path.c_str(),FILE_READ);if(!f)return 0;
  uint32_t signature=uint32_t(f.size())^uint32_t(f.getLastWrite());uint8_t sample[512];size_t n=f.read(sample,sizeof(sample));signature^=readerCRC(sample,n);
  if(f.size()>512){f.seek(f.size()-512);n=f.read(sample,sizeof(sample));signature^=readerCRC(sample,n);}f.close();return signature;
}
BookMetadata cachedMetadata(const std::string &path,bool cardReady){
  BookMetadata m;m.title=path.substr(path.find_last_of('/')+1);if(!cardReady)return m;
  m.signature=fileSignature(path);std::string name=cacheName(path,".json");
  File f=SD.open(name.c_str(),FILE_READ);
  if(f&&f.size()>0&&f.size()<8192){
    std::string text(f.size(),'\0');f.readBytes(&text[0],text.size());f.close();cJSON *doc=cJSON_Parse(text.c_str());
    cJSON *sig=cJSON_GetObjectItemCaseSensitive(doc,"signature"),*title=cJSON_GetObjectItemCaseSensitive(doc,"title"),*author=cJSON_GetObjectItemCaseSensitive(doc,"author"),*cover=cJSON_GetObjectItemCaseSensitive(doc,"cover"),*count=cJSON_GetObjectItemCaseSensitive(doc,"chapters");
    if(cJSON_IsNumber(sig)&&uint32_t(sig->valuedouble)==m.signature&&cJSON_IsString(title)&&cJSON_IsString(author)&&cJSON_IsString(cover)&&cJSON_IsNumber(count)){
      m.title=title->valuestring;m.author=author->valuestring;m.cover=cover->valuestring;m.chapters=count->valueint;m.valid=true;cJSON_Delete(doc);Serial.println("CACHE metadata hit");return m;
    }cJSON_Delete(doc);
  }else f.close();
  Epub epub("/sd"+path);if(!epub.load())return m;
  m.title=epub.get_title();m.author=epub.get_author();m.cover=epub.get_cover_image_item();m.chapters=epub.get_spine_items_count();m.valid=true;
  cJSON *doc=cJSON_CreateObject();if(doc){
    cJSON_AddNumberToObject(doc,"signature",m.signature);cJSON_AddStringToObject(doc,"title",m.title.c_str());cJSON_AddStringToObject(doc,"author",m.author.c_str());cJSON_AddStringToObject(doc,"cover",m.cover.c_str());cJSON_AddNumberToObject(doc,"chapters",m.chapters);
    char *text=cJSON_PrintUnformatted(doc);if(text){SD.mkdir("/.crowreader/cache");std::string temp=name+".tmp";SD.remove(temp.c_str());File out=SD.open(temp.c_str(),FILE_WRITE);if(out){bool written=out.print(text)==strlen(text);out.flush();out.close();if(written){SD.remove(name.c_str());SD.rename(temp.c_str(),name.c_str());}}cJSON_free(text);}cJSON_Delete(doc);
  }return m;
}
struct ImageHeader {uint32_t magic,width,height,crc;};
bool cachedImage(Epub &epub,const std::string &href,uint8_t *canvas,int cw,int ch,int x,int y,int w,int h,bool cardReady){
  if(w<=0||h<=0||w>400||h>400)return false;
  std::string bookPath=epub.get_path();if(bookPath.compare(0,3,"/sd")==0)bookPath.erase(0,3);
  uint32_t signature=cardReady?fileSignature(bookPath):0;
  std::string key="image-v3:"+bookPath+":"+std::to_string(signature)+":"+href+":"+std::to_string(w)+"x"+std::to_string(h);
  std::string name=cacheName(key,".bin");size_t size=((w+7)/8)*h;
  uint8_t *bits=(uint8_t *)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!bits)return false;
  memset(bits,0xFF,size);bool ok=false;ImageHeader header{};File f;
  if(cardReady){f=SD.open(name.c_str(),FILE_READ);if(f&&f.size()==sizeof(header)+size&&f.read((uint8_t *)&header,sizeof(header))==sizeof(header)&&header.magic==0x494D4732&&header.width==uint32_t(w)&&header.height==uint32_t(h)&&f.read(bits,size)==size&&readerCRC(bits,size)==header.crc){ok=true;Serial.println("CACHE image hit");}f.close();}
  if(!ok){
    size_t length=0;uint8_t *data=epub.get_item_contents(href,&length);
    ok=renderBookImage(bits,data,length,0,0,w,h,w,h);free(data);
    if(ok&&cardReady){
      header={0x494D4732,uint32_t(w),uint32_t(h),readerCRC(bits,size)};SD.mkdir("/.crowreader/cache");
      std::string temp=name+".tmp";SD.remove(temp.c_str());File out=SD.open(temp.c_str(),FILE_WRITE);
      if(out){bool written=out.write((uint8_t *)&header,sizeof(header))==sizeof(header)&&out.write(bits,size)==size;out.flush();out.close();if(written){SD.remove(name.c_str());SD.rename(temp.c_str(),name.c_str());}}
    }
  }
  if(ok)for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
    int dx=x+xx,dy=y+yy;if(dx<0||dx>=cw||dy<0||dy>=ch)continue;
    uint8_t mask=0x80>>(dx&7);uint8_t &dest=canvas[dy*((cw+7)/8)+dx/8];
    if(bits[yy*((w+7)/8)+xx/8]&(0x80>>(xx&7)))dest|=mask;else dest&=~mask;
  }
  free(bits);return ok;
}

bool readerCacheSelfTest(Epub &epub,const std::string &href,bool cardReady){
  if(!cardReady||href.empty())return false;std::string path=epub.get_path();if(path.compare(0,3,"/sd")==0)path.erase(0,3);
  std::string key="image-v3:"+path+":"+std::to_string(fileSignature(path))+":"+href+":64x64",name=cacheName(key,".bin");
  uint8_t bits[512],again[512];memset(bits,255,sizeof(bits));memset(again,255,sizeof(again));SD.remove(name.c_str());
  bool ok=cachedImage(epub,href,bits,64,64,0,0,64,64,cardReady);uint32_t crc=readerCRC(bits,sizeof(bits));
  ok=cachedImage(epub,href,again,64,64,0,0,64,64,cardReady)&&readerCRC(again,sizeof(again))==crc&&ok;
  SD.remove(name.c_str());File corrupt=SD.open(name.c_str(),FILE_WRITE);corrupt.print("corrupt cache");corrupt.close();
  std::string temp=name+".tmp";File pending=SD.open(temp.c_str(),FILE_WRITE);pending.print("interrupted cache");pending.close();
  memset(again,255,sizeof(again));ok=cachedImage(epub,href,again,64,64,0,0,64,64,cardReady)&&readerCRC(again,sizeof(again))==crc&&ok;
  SD.remove(name.c_str());SD.remove(temp.c_str());Serial.printf("SELFTEST cache hit/corrupt/interrupted pass=%d\n",ok);return ok;
}
