#include "ReaderFontLab.h"
#include "ReaderFontLabFrames.h"
#include "miniz.h"
bool ReaderFontLab::draw(uint8_t *canvas,size_t capacity,int width,int height)const{
  if(!canvas||!((width==400&&height==300)||(width==300&&height==400)))return false;
  const size_t length=size_t((width+7)/8)*height;
  if(capacity<length)return false;
  const int style=reference_?0:index_;
  const int offset=bank_?6:0;
  const auto &asset=fontLabFrames[(offset+style)*2+(width==300)];
  if(asset.decoded!=length)return false;
  if(tinfl_decompress_mem_to_mem(canvas,length,asset.data,asset.length,TINFL_FLAG_PARSE_ZLIB_HEADER)!=length)return false;
  return uint32_t(mz_crc32(0,canvas,length))==asset.crc;
}
