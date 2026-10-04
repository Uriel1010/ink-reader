#pragma once
#include <stdint.h>
#include <string>
// Invalid sequences consume only their valid prefix, preserving subsequent text.
inline uint32_t readerNextCodepoint(const std::string &text,size_t &cursor){
  if(cursor>=text.size())return 0;uint8_t lead=text[cursor++];if(lead<128)return lead;
  int count=lead>=0xC2&&lead<=0xDF?1:lead>=0xE0&&lead<=0xEF?2:lead>=0xF0&&lead<=0xF4?3:0;
  if(!count)return 0xFFFD;int remaining=count;uint32_t value=lead&((1<<(6-count))-1);
  while(remaining--){if(cursor>=text.size()||(uint8_t(text[cursor])&0xC0)!=0x80)return 0xFFFD;value=(value<<6)|(uint8_t(text[cursor++])&63);}
  uint32_t minimum=count==1?128:count==2?2048:65536;
  return value<minimum||value>0x10FFFF||(value>=0xD800&&value<=0xDFFF)?0xFFFD:value;
}
