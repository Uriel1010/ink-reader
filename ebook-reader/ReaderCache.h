#pragma once
#include <string>
#include <stdint.h>
#include <stddef.h>
class Epub;
struct BookMetadata {std::string title,author,cover;uint32_t signature=0;int chapters=0;bool valid=false;};
uint32_t readerHash(const std::string &text);
uint32_t readerCRC(const uint8_t *data,size_t size);
BookMetadata cachedMetadata(const std::string &path,bool cardReady);
bool cachedImage(Epub &epub,const std::string &href,uint8_t *canvas,int canvasWidth,int canvasHeight,int x,int y,int width,int height,bool cardReady);

bool readerCacheSelfTest(Epub &epub,const std::string &href,bool cardReady);
