#include "ReaderTransferQR.h"
#include <qrcode.h>
namespace {int left,top,extent;void (*draw)(int,int);
void paint(esp_qrcode_handle_t code){int count=esp_qrcode_get_size(code),scale=extent/(count+8);if(scale<1)return;int offset=(extent-(count+8)*scale)/2+4*scale;for(int y=0;y<count;y++)for(int x=0;x<count;x++)if(esp_qrcode_get_module(code,x,y))for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++)draw(left+offset+x*scale+dx,top+offset+y*scale+dy);}
}
void readerTransferQR(const std::string &text,int x,int y,int size,void (*pixel)(int,int)){left=x;top=y;extent=size;draw=pixel;esp_qrcode_config_t config={paint,6,ESP_QRCODE_ECC_MED};esp_qrcode_generate(&config,text.c_str());}
