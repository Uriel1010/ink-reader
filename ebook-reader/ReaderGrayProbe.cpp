#include <Arduino.h>
#include <esp_heap_caps.h>
#include "EPD.h"
#include "miniz.h"
#include "ReaderGrayProbe.h"
#include "ReaderGrayProbeAssets.h"
bool readerGrayProbePresent(int rotation,uint32_t &elapsed){
  if(rotation!=0&&rotation!=90&&rotation!=180&&rotation!=270)return false;
  uint8_t *pixels=(uint8_t *)heap_caps_malloc(30000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!pixels)return false;
  bool portrait=rotation%180;int width=portrait?300:400;
  const uint8_t *data=portrait?grayProbe300:grayProbe400;
  size_t length=portrait?sizeof(grayProbe300):sizeof(grayProbe400);
  uint32_t crc=portrait?grayProbeCRC300:grayProbeCRC400;
  if(tinfl_decompress_mem_to_mem(pixels,30000,data,length,TINFL_FLAG_PARSE_ZLIB_HEADER)!=30000||uint32_t(mz_crc32(0,pixels,30000))!=crc){free(pixels);return false;}
  bool result=readerGrayFramePresent(pixels,rotation,elapsed);free(pixels);return result;
}
bool readerGrayFramePresent(const uint8_t *pixels,int rotation,uint32_t &elapsed){
  if(!pixels||(rotation!=0&&rotation!=90&&rotation!=180&&rotation!=270))return false;
  int width=rotation%180?300:400;
  uint32_t start=millis();EPD_ClearError();EPD_RESET();EPD_ReadBusy();
  if(EPD_HasError())return false;
  EPD_WR_REG(0x12);EPD_ReadBusy();if(EPD_HasError())return false;
  // Exact Waveshare epd4in2_V2 Init_4Gray register values and LUT.
  EPD_WR_REG(0x21);EPD_WR_DATA8(0);EPD_WR_DATA8(0);
  EPD_WR_REG(0x3C);EPD_WR_DATA8(0x03);
  const uint8_t booster[]={0x8B,0x9C,0xA4,0x0F};
  EPD_WR_REG(0x0C);for(uint8_t value:booster)EPD_WR_DATA8(value);
  EPD_WR_REG(0x32);for(int i=0;i<227;i++)EPD_WR_DATA8(grayProbeLut[i]);
  EPD_WR_REG(0x3F);EPD_WR_DATA8(grayProbeLut[227]);
  EPD_WR_REG(0x03);EPD_WR_DATA8(grayProbeLut[228]);
  EPD_WR_REG(0x04);for(int i=229;i<=231;i++)EPD_WR_DATA8(grayProbeLut[i]);
  EPD_WR_REG(0x2C);EPD_WR_DATA8(grayProbeLut[232]);
  EPD_WR_REG(0x11);EPD_WR_DATA8(3);EPD_Address_Set(0,0,399,299);EPD_SetCursor(0,0);EPD_ReadBusy();
  if(EPD_HasError())return false;
  for(int plane=0;plane<2;plane++){
    EPD_SetCursor(0,0);EPD_WR_REG(plane?0x26:0x24);
    for(int py=0;py<300;py++)for(int byte=0;byte<50;byte++){
      uint8_t result=0;
      for(int bit=0;bit<8;bit++){
        int px=byte*8+bit,x=px,y=py;
        if(rotation==90){x=py;y=399-px;}else if(rotation==180){x=399-px;y=299-py;}else if(rotation==270){x=299-py;y=px;}
        size_t pos=size_t(y)*width+x;uint8_t shade=(pixels[pos/4]>>(6-2*(pos%4)))&3;
        if(plane?(shade&1):(shade&2))result|=0x80>>bit;
      }
      EPD_WR_DATA8(result);
    }
  }
  EPD_WR_REG(0x22);EPD_WR_DATA8(0xCF);EPD_WR_REG(0x20);EPD_ReadBusy();elapsed=millis()-start;
  return !EPD_HasError();
}
