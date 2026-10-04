#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include "ReaderGallery.h"
#include "ReaderCache.h"
#include "BookImages.h"

static void scanImages(const std::string &path,int depth,std::vector<std::string> &images){
  File directory=SD.open(path.c_str(),FILE_READ);if(!directory||!directory.isDirectory())return;
  File file;while(images.size()<128&&(file=directory.openNextFile())){
    std::string name=file.name();size_t slash=name.find_last_of('/');if(slash!=std::string::npos)name=name.substr(slash+1);
    if(name.empty()||name[0]=='.'||name.find(".crupload")!=std::string::npos||name.find(".crbackup")!=std::string::npos){file.close();continue;}
    std::string full=path=="/"?"/"+name:path+"/"+name;bool directoryEntry=file.isDirectory();file.close();
    if(directoryEntry){if(depth<5)scanImages(full,depth+1,images);}else{std::string extension=name.substr(name.find_last_of('.')==std::string::npos?name.size():name.find_last_of('.'));for(char &character:extension)character=tolower((unsigned char)character);if(extension==".jpg"||extension==".jpeg"||extension==".png")images.push_back(full);}
  }directory.close();
}
std::vector<std::string> scanGallery(bool cardReady){std::vector<std::string> images;if(cardReady)scanImages("/",0,images);std::sort(images.begin(),images.end());return images;}
void invalidateReaderCaches(){
  File directory=SD.open("/.crowreader/cache",FILE_READ);if(!directory)return;File file;
  while((file=directory.openNextFile())){std::string path=file.path();bool regular=!file.isDirectory();file.close();if(regular)SD.remove(path.c_str());}directory.close();
}
struct GalleryHeader{uint32_t magic,width,height,crc;};
bool galleryImage(const std::string &path,uint8_t *canvas,int cw,int ch,int x,int y,int width,int height){
  if(width<=0||height<=0||width>400||height>400)return false;
  File source=SD.open(path.c_str(),FILE_READ);if(!source||source.isDirectory()||source.size()<8||source.size()>1024*1024){source.close();return false;}
  uint32_t signature=uint32_t(source.size())^uint32_t(source.getLastWrite());uint8_t sample[512];size_t count=source.read(sample,sizeof(sample));signature^=readerCRC(sample,count);
  if(source.size()>512){source.seek(source.size()-512);count=source.read(sample,sizeof(sample));signature^=readerCRC(sample,count);}source.seek(0);
  std::string key="gallery-v2:"+path+":"+std::to_string(signature)+":"+std::to_string(width)+"x"+std::to_string(height);char cache[90];snprintf(cache,sizeof(cache),"/.crowreader/cache/gallery-%08lx.bin",(unsigned long)readerHash(key));
  size_t size=((width+7)/8)*height;uint8_t *bits=(uint8_t *)heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!bits){source.close();return false;}memset(bits,255,size);
  GalleryHeader header{};File file=SD.open(cache,FILE_READ);bool ok=file&&file.size()==sizeof(header)+size&&file.read((uint8_t *)&header,sizeof(header))==sizeof(header)&&header.magic==0x47414C31&&header.width==uint32_t(width)&&header.height==uint32_t(height)&&file.read(bits,size)==size&&readerCRC(bits,size)==header.crc;file.close();
  if(!ok){size_t length=source.size();uint8_t *data=(uint8_t *)heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(data){bool read=source.read(data,length)==length;source.close();ok=read&&renderBookImage(bits,data,length,0,0,width,height,width,height);free(data);}else source.close();
    if(ok){SD.mkdir("/.crowreader/cache");std::string pending=std::string(cache)+".tmp";SD.remove(pending.c_str());file=SD.open(pending.c_str(),FILE_WRITE);header={0x47414C31,uint32_t(width),uint32_t(height),readerCRC(bits,size)};bool written=file&&file.write((uint8_t *)&header,sizeof(header))==sizeof(header)&&file.write(bits,size)==size;file.flush();file.close();if(written){SD.remove(cache);SD.rename(pending.c_str(),cache);}}
  }else{source.close();Serial.println("CACHE gallery hit");}
  if(ok)for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++){int dx=x+xx,dy=y+yy;if(dx<0||dx>=cw||dy<0||dy>=ch)continue;uint8_t &destination=canvas[dy*((cw+7)/8)+dx/8],mask=0x80>>(dx&7);if(bits[yy*((width+7)/8)+xx/8]&(0x80>>(xx&7)))destination|=mask;else destination&=~mask;}
  free(bits);return ok;
}
