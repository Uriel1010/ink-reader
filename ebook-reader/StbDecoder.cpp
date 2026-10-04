#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdint.h>
#include <string.h>
#include "StbDecoder.h"
static size_t allocated=0;
struct alignas(16) Allocation {size_t size;};
static void *imageAlloc(size_t size){
  if(size>7*1024*1024||allocated+size>7*1024*1024)return nullptr;
  auto *p=(Allocation *)heap_caps_malloc(size+sizeof(Allocation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!p)return nullptr;p->size=size;allocated+=size;return p+1;
}
static void imageFree(void *pointer){if(!pointer)return;auto *p=(Allocation *)pointer-1;allocated-=p->size;heap_caps_free(p);}
static void *imageRealloc(void *pointer,size_t size){
  if(!pointer)return imageAlloc(size);
  auto *old=(Allocation *)pointer-1;size_t oldSize=old->size;
  if(size>7*1024*1024||allocated-oldSize+size>7*1024*1024)return nullptr;
  auto *p=(Allocation *)heap_caps_realloc(old,size+sizeof(Allocation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!p)return nullptr;p->size=size;allocated=allocated-oldSize+size;return p+1;
}
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC imageAlloc
#define STBI_REALLOC imageRealloc
#define STBI_FREE imageFree
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
uint8_t *decodeImageFallback(const uint8_t *data,size_t size,int &width,int &height){
  int channels=0;
  if(size>1024*1024||!stbi_info_from_memory(data,size,&width,&height,&channels)||width<=0||height<=0||int64_t(width)*height>5000000)return nullptr;
  uint8_t *pixels=stbi_load_from_memory(data,size,&width,&height,&channels,(data[0]==0xFF?1:2));
  Serial.printf("IMAGE stb dimensions=%dx%d allocated=%u success=%d\n",width,height,unsigned(allocated),pixels!=nullptr);return pixels;
}
void freeImageFallback(uint8_t *pixels){imageFree(pixels);}

