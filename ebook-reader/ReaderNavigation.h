#pragma once
#include <stdint.h>
#include "ReaderFeatures.h"
// Physical events are mapped once; serial next/previous are already logical.
inline uint8_t readerLogicalButton(uint8_t physical,int rotation){
  return (physical==2||physical==3)&&rotation>=180?5-physical:physical;
}
inline int readerWrapSelection(int current,int delta,int count){
  if(count<=0)return 0;int value=(current+delta)%count;return value<0?value+count:value;
}
static const int readerMainSettings[]={0,11,1,2,3,4,5,10,9,12
#if READER_ENABLE_CLOCK_SCREENSAVER
,14
#endif
};
static const int readerAdvancedSettings[]={6,7,8,13};
inline int readerSettingsCount(bool advanced){return advanced?4:int(sizeof(readerMainSettings)/sizeof(readerMainSettings[0]));}
inline int readerSettingId(bool advanced,int row){return advanced?readerAdvancedSettings[row]:readerMainSettings[row];}

inline int readerMenuCount(){return READER_ENABLE_CLOCK_SCREENSAVER?7:6;}
