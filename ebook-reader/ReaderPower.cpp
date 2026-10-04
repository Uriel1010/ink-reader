#include <Arduino.h>
#include <SD.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include "ReaderPower.h"
#include "ReaderStorage.h"
#include "ReaderCache.h"
#include "EPD.h"
static const uint8_t pins[]={2,1,4,6,5};
struct Retained {uint32_t magic,generation,frameCRC,sessionCRC,cleanup,slot;uint8_t injected;uint8_t padding[3];uint32_t crc;};
RTC_DATA_ATTR static Retained retained{};
struct FrameHeader {uint32_t magic,generation,crc,length;};
static bool validRetained(){return retained.magic==0x43525732&&retained.crc==readerCRC((uint8_t *)&retained,offsetof(Retained,crc));}
bool readerWokeFromSleep(){
  auto cause=esp_sleep_get_wakeup_cause();
  return validRetained()&&(cause==ESP_SLEEP_WAKEUP_EXT1||cause==ESP_SLEEP_WAKEUP_TIMER);
}
uint8_t readerWakeButton(){
  if(!readerWokeFromSleep())return 255;
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER)return retained.injected;
  uint64_t status=esp_sleep_get_ext1_wakeup_status();uint8_t key=255;int count=0;
  for(uint8_t i=0;i<5;i++)if(status&(1ULL<<pins[i])){key=i;++count;}
  return count==1?key:255;
}
void readerReleaseSleepHolds(){
  gpio_deep_sleep_hold_dis();
  for(uint8_t pin:{uint8_t(7),uint8_t(41),uint8_t(42)}){gpio_hold_dis((gpio_num_t)pin);if(rtc_gpio_is_valid_gpio((gpio_num_t)pin)){rtc_gpio_hold_dis((gpio_num_t)pin);rtc_gpio_deinit((gpio_num_t)pin);}}
  for(uint8_t pin:pins)rtc_gpio_deinit((gpio_num_t)pin);
}
static std::string framePath(uint32_t slot){return "/.crowreader/sleep-frame-"+std::to_string(slot)+".bin";}
bool readerRestoreFrame(uint8_t *frame,unsigned &cleanup){
  if(!readerWokeFromSleep())return false;
  File f=SD.open(framePath(retained.slot).c_str(),FILE_READ);FrameHeader header{};
  bool ok=f&&f.size()==sizeof(header)+15000&&f.read((uint8_t *)&header,sizeof(header))==sizeof(header)&&header.magic==0x46524D32&&header.generation==retained.generation&&header.crc==retained.frameCRC&&header.length==15000&&f.read(frame,15000)==15000&&readerCRC(frame,15000)==header.crc;
  f.close();cleanup=ok?retained.cleanup:0;Serial.printf("SLEEP frame_restore=%d cleanup=%u\n",ok,cleanup);return ok;
}
std::string readerSleepSession(ReaderStorage &storage){
  if(!readerWokeFromSleep())return "";
  std::string text=storage.loadDocument("sleep-session.json");
  return readerCRC((uint8_t *)text.data(),text.size())==retained.sessionCRC?text:"";
}
bool readerEnterSleep(ReaderStorage &storage,const uint8_t *frame,const std::string &session,unsigned cleanup,void (*shutdown)(),uint32_t timerSeconds,uint8_t injectedButton,bool (*ready)()){
  // Level wake requires every button to be released before sleeping.
  uint32_t releasedAt=millis();
  while(uint32_t(millis()-releasedAt)<150){
    bool pressed=false;for(uint8_t pin:pins)if(digitalRead(pin)==LOW)pressed=true;
    if(pressed){Serial.println("SLEEP deferred: button held");return false;}delay(5);
  }
  uint32_t slot=validRetained()?retained.slot^1:0,generation=validRetained()?retained.generation+1:1;
  FrameHeader header{0x46524D32,generation,readerCRC(frame,15000),15000};
  std::string path=framePath(slot),temp=path+".tmp";SD.remove(temp.c_str());
  File f=SD.open(temp.c_str(),FILE_WRITE);
  bool ok=f&&f.write((uint8_t *)&header,sizeof(header))==sizeof(header)&&f.write(frame,15000)==15000;
  f.flush();f.close();if(!ok)return false;
  f=SD.open(temp.c_str(),FILE_READ);FrameHeader check{};uint8_t block[512];uint32_t crc=~0u;
  ok=f&&f.read((uint8_t *)&check,sizeof(check))==sizeof(check)&&check.crc==header.crc&&check.generation==generation;
  size_t count=0;while(ok&&f.available()){size_t n=f.read(block,sizeof(block));count+=n;for(size_t i=0;i<n;i++){crc^=block[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&-(crc&1));}}
  f.close();ok=ok&&count==15000&&~crc==header.crc;if(!ok)return false;
  SD.remove(path.c_str());if(!SD.rename(temp.c_str(),path.c_str()))return false;
  if(!storage.saveDocument("sleep-session.json",session))return false;
  if(ready&&!ready())return false;
  uint64_t mask=0;for(uint8_t pin:pins)mask|=1ULL<<pin;
  if(esp_sleep_enable_ext1_wakeup_io(mask,ESP_EXT1_WAKEUP_ANY_LOW)!=ESP_OK)return false;
  if(timerSeconds)esp_sleep_enable_timer_wakeup(uint64_t(timerSeconds)*1000000);
  retained={0x43525732,generation,header.crc,readerCRC((uint8_t *)session.data(),session.size()),cleanup,slot,injectedButton,{0,0,0},0};
  retained.crc=readerCRC((uint8_t *)&retained,offsetof(Retained,crc));
  if(shutdown)shutdown();
  EPD_Sleep();
  // Schematic: Q9/Q10 switch peripheral grounds. Float SPI lines to avoid back-powering.
  for(uint8_t pin:{uint8_t(12),uint8_t(11),uint8_t(47),uint8_t(46),uint8_t(45),uint8_t(39),uint8_t(13),uint8_t(40),uint8_t(10)})pinMode(pin,INPUT);
  for(uint8_t pin:{uint8_t(7),uint8_t(41),uint8_t(42)}){pinMode(pin,OUTPUT);digitalWrite(pin,LOW);gpio_hold_en((gpio_num_t)pin);}
  gpio_deep_sleep_hold_en();
  for(uint8_t pin:pins){rtc_gpio_init((gpio_num_t)pin);rtc_gpio_set_direction((gpio_num_t)pin,RTC_GPIO_MODE_INPUT_ONLY);rtc_gpio_pullup_en((gpio_num_t)pin);rtc_gpio_pulldown_dis((gpio_num_t)pin);}
  Serial.printf("SLEEP enter timer=%u injected=%u cleanup=%u\n",timerSeconds,injectedButton,cleanup);Serial.flush();
  esp_deep_sleep_start();return true;
}
