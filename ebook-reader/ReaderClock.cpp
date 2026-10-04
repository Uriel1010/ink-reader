#include "ReaderClock.h"
#if READER_ENABLE_CLOCK_SCREENSAVER
#include <Arduino.h>
#include <esp_wifi.h>
#include "ReaderRadio.h"
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <time.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>
#include "ReaderClockFont.h"
#include "miniz.h"
namespace {
constexpr uint64_t second=1000000,day=86400*second,hour=3600*second,timeout=20*second;
const char *zone="IST-2IDT,M3.4.4/26,M10.5.0";
std::atomic<bool> synchronized{false},gotAddress{false};
bool netReady=false,radioEnabled=false;
void networkEvent(void *,esp_event_base_t base,int32_t id,void *){
  if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)gotAddress.store(true);
  if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED)gotAddress.store(false);
}
bool initializeNetwork(){
  if(netReady)return true;
  if(!readerNetworkReady())return false;
  if(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,networkEvent,nullptr)!=ESP_OK)return false;
  if(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,networkEvent,nullptr)!=ESP_OK)return false;
  netReady=true;return true;
}
uint64_t uptime(){return esp_timer_get_time();}
bool elapsed(uint64_t now,uint64_t start,uint64_t period){return now>=start&&now-start>=period;}
bool acceptable(int64_t epoch){return epoch>=1577836800LL&&epoch<4102444800LL;}
void synced(struct timeval *){synchronized.store(true);}
const char *months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
std::string dateText(const tm &t){return std::string(months[t.tm_mon])+" "+std::to_string(t.tm_mday)+", "+std::to_string(t.tm_year+1900);}
const ClockGlyph &glyph(char ch){const char *found=strchr(clockCharacters,ch);if(!found)found=strchr(clockCharacters,'?');return clockGlyphs[found-clockCharacters];}
const unsigned char *fontBits(){static std::vector<unsigned char> decoded;static bool attempted=false;if(!attempted){attempted=true;decoded.resize(clockFontSize);if(tinfl_decompress_mem_to_mem(decoded.data(),decoded.size(),clockFontBits,sizeof(clockFontBits),TINFL_FLAG_PARSE_ZLIB_HEADER)!=clockFontSize)decoded.clear();}return decoded.empty()?nullptr:decoded.data();}
struct Bounds {int left=0,top=0,right=0,bottom=0;};
Bounds bounds(const std::string &text){
  Bounds b{9999,9999,-9999,-9999};int x=0;
  for(char ch:text){const auto &g=glyph(ch);if(g.w&&g.h){b.left=std::min(b.left,x+g.x);b.right=std::max(b.right,x+g.x+g.w);b.top=std::min(b.top,int(g.y));b.bottom=std::max(b.bottom,g.y+g.h);}x+=g.advance;}
  return b;
}
bool ink(const ClockGlyph &g,int x,int y){
  if(x<0||y<0||x>=g.w||y>=g.h)return false;int bit=y*g.w+x;const auto *bits=fontBits();return bits&&(bits[g.offset+bit/8]&(128>>(bit%8)));
}
void fitText(const std::string &text,int x,int y,int width,int height,ReaderClock::Pixel pixel){
  Bounds b=bounds(text);if(b.right<=b.left||b.bottom<=b.top)return;
  float scale=std::min(float(width)/(b.right-b.left),float(height)/(b.bottom-b.top));
  int origin=x+(width-int(std::round((b.right-b.left)*scale)))/2,advance=0;
  for(char ch:text){
    const auto &g=glyph(ch);float gx=advance+g.x-b.left,gy=g.y-b.top;
    for(int dy=int(std::floor(gy*scale));dy<int(std::ceil((gy+g.h)*scale));++dy)
      for(int dx=int(std::floor(gx*scale));dx<int(std::ceil((gx+g.w)*scale));++dx){
        float x0=dx/scale-gx,x1=(dx+1)/scale-gx,y0=dy/scale-gy,y1=(dy+1)/scale-gy,black=0;
        for(int sy=int(std::floor(y0));sy<int(std::ceil(y1));++sy)for(int sx=int(std::floor(x0));sx<int(std::ceil(x1));++sx)
          if(ink(g,sx,sy))black+=std::max(0.0f,std::min(x1,float(sx+1))-std::max(x0,float(sx)))*std::max(0.0f,std::min(y1,float(sy+1))-std::max(y0,float(sy)));
        if(black>=(x1-x0)*(y1-y0)*0.45f)pixel(origin+dx,y+dy);
      }
    advance+=g.advance;
  }
}
void polygon(const uint8_t points[][2],int count,float scale,int x,int y,ReaderClock::Pixel pixel){
  int vertices[6][2],left=9999,top=9999,right=0,bottom=0;
  for(int i=0;i<count;++i){
    vertices[i][0]=int(std::round(points[i][0]*scale));vertices[i][1]=int(std::round(points[i][1]*scale));
    left=std::min(left,vertices[i][0]);top=std::min(top,vertices[i][1]);right=std::max(right,vertices[i][0]);bottom=std::max(bottom,vertices[i][1]);
  }
  for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
    bool plus=false,minus=false;
    for(int i=0;i<count;++i){int j=(i+1)%count;int cross=(vertices[j][0]-vertices[i][0])*(2*py+1-2*vertices[i][1])-(vertices[j][1]-vertices[i][1])*(2*px+1-2*vertices[i][0]);plus|=cross>0;minus|=cross<0;}
    if(!(plus&&minus))pixel(x+px,y+py);
  }
}
void segment(int index,float scale,int x,int y,ReaderClock::Pixel pixel){
  static const uint8_t shapes[7][6][2]={
    {{6,5},{10,1},{38,1},{42,5},{38,9},{10,9}},
    {{44,7},{48,11},{48,37},{44,41},{40,37},{40,11}},
    {{44,49},{48,53},{48,79},{44,83},{40,79},{40,53}},
    {{6,85},{10,81},{38,81},{42,85},{38,89},{10,89}},
    {{4,49},{8,53},{8,79},{4,83},{0,79},{0,53}},
    {{4,7},{8,11},{8,37},{4,41},{0,37},{0,11}},
    {{6,45},{10,41},{38,41},{42,45},{38,49},{10,49}}
  };polygon(shapes[index],6,scale,x,y,pixel);
}
void clockFace(int64_t epoch,int width,int height,ReaderClock::Pixel pixel){
  time_t stamp=epoch;tm t{};localtime_r(&stamp,&t);std::string date=dateText(t);
  int clockWidth=width-40;float scale=float(clockWidth)/244;
  Bounds db=bounds(date);int dateHeight=int(std::ceil((db.bottom-db.top)*float(clockWidth)/(db.right-db.left)));
  int clockHeight=int(std::ceil(90*scale)),gap=24,top=(height-clockHeight-gap-dateHeight)/2,left=(width-clockWidth)/2;
  static const uint8_t masks[]={63,6,91,79,102,109,125,7,127,111};
  int digits[]={t.tm_hour/10,t.tm_hour%10,t.tm_min/10,t.tm_min%10},positions[]={0,60,136,196};
  for(int d=0;d<4;++d)for(int s=0;s<7;++s)if(masks[digits[d]]&(1<<s))segment(s,scale,left+int(std::round(positions[d]*scale)),top,pixel);
  const uint8_t colon[4][2]={{118,24},{126,24},{126,32},{118,32}};polygon(colon,4,scale,left,top,pixel);polygon(colon,4,scale,left,top+int(std::round(36*scale)),pixel);
  fitText(date,left,top+clockHeight+gap,clockWidth,dateHeight,pixel);
}
}
void ReaderClock::begin(){setenv("TZ",zone,1);tzset();}
bool ReaderClock::configure(const std::string &ssid,const std::string &password,bool clear){
  if(!clear){
    if(ssid.empty()||ssid.size()>32||ssid.find('\0')!=std::string::npos||password.find('\0')!=std::string::npos)return false;
    bool hex=password.size()==64&&std::all_of(password.begin(),password.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
    if(!hex&&(password.size()<8||password.size()>63))return false;
  }
  Preferences p;if(!p.begin("crowclock",false))return false;
  std::string packed=ssid+char(0)+password+char(0);bool ok=clear?p.clear():p.putBytes("network",packed.data(),packed.size())==packed.size();p.end();return ok;
}
void ReaderClock::startSync(){
  stopSync();lastAttempt=started=uptime();phase=1;
  Preferences p;if(!p.begin("crowclock",true)){phase=0;return;}
  size_t size=p.getBytesLength("network");std::vector<char> packed(size<=100?size:0);
  bool read=size>=10&&size<=100&&p.getBytes("network",packed.data(),size)==size;p.end();
  if(!read||packed.back()!=0){phase=0;return;}
  size_t split=0;while(split<size&&packed[split])++split;
  if(split==0||split>32||split+1>=size){phase=0;return;}
  if(!initializeNetwork()){phase=0;return;}
  if(!readerRadioStart(false)){phase=0;return;}
  radioEnabled=true;gotAddress.store(false);
  wifi_config_t config{};memcpy(config.sta.ssid,packed.data(),split);
  size_t passwordLength=size-split-2;if(passwordLength<8||passwordLength>64){stopSync();return;}
  memcpy(config.sta.password,packed.data()+split+1,passwordLength);
  config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;config.sta.pmf_cfg.capable=true;
  if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_set_config(WIFI_IF_STA,&config)!=ESP_OK||esp_wifi_start()!=ESP_OK||esp_wifi_connect()!=ESP_OK)stopSync();
}
void ReaderClock::stopSync(){
  if(netReady){esp_sntp_stop();esp_sntp_set_time_sync_notification_cb(nullptr); }
  if(radioEnabled){readerRadioStop();radioEnabled=false;}
  gotAddress.store(false);
  synchronized.store(false);phase=0;
}
void ReaderClock::enter(bool synchronize){running=true;previewing=false;drawnMinute=-1;lastStatus=-1;lastCleanup=uptime();if(synchronize)startSync();}
void ReaderClock::leave(){stopSync();running=false;previewing=false;}
void ReaderClock::retry(){if(running){previewing=false;startSync();}}
int64_t ReaderClock::now() const{return previewing?previewEpoch+(uptime()-previewStarted)/second:time(nullptr);}
int64_t ReaderClock::minute() const{return valid()?now()/60:-1;}
bool ReaderClock::radioOn() const{wifi_mode_t mode=WIFI_MODE_NULL;return radioEnabled&&esp_wifi_get_mode(&mode)==ESP_OK&&mode!=WIFI_MODE_NULL;}
bool ReaderClock::tick(){
  if(!running)return false;uint64_t current=uptime();
  if(phase==1&&gotAddress.load()){
    synchronized.store(false);esp_sntp_set_time_sync_notification_cb(synced);esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_init();phase=2;
  }
  if(phase&&synchronized.exchange(false)&&acceptable(time(nullptr))){trusted=true;stopSync();Serial.println("CLOCK sync=ok wifi=off");}
  if(phase&&elapsed(current,started,timeout)){stopSync();Serial.println("CLOCK sync=timeout wifi=off");}
  if(!previewing&&!phase&&elapsed(current,lastAttempt,day))startSync();
  int status=valid()?0:syncing()?1:2;int64_t currentMinute=minute();
  bool changed=status!=lastStatus||(status==0&&currentMinute!=drawnMinute);
  if(changed){lastStatus=status;drawnMinute=currentMinute;}return changed;
}
void ReaderClock::draw(int width,int height,Pixel pixel) const{
  if(valid()){clockFace(now(),width,height,pixel);return;}
  const char *message=syncing()?"Setting clock":"Time unavailable";
  fitText(message,24,height/2-35,width-48,44,pixel);
  fitText(syncing()?"Any button: return":"Menu: retry   Exit: return",24,height/2+32,width-48,22,pixel);
}
bool ReaderClock::cleanupDue() const{return running&&elapsed(uptime(),lastCleanup,hour);}
void ReaderClock::presented(bool full){if(full)lastCleanup=uptime();}
void ReaderClock::previewAt(int64_t epoch){stopSync();running=true;previewing=true;previewEpoch=epoch;previewStarted=uptime();drawnMinute=-1;lastStatus=-1;}
int ReaderClock::previewCharacter(char input,int64_t &epoch){
  static bool collecting=false;static std::string value;
  if(input==':'){collecting=true;value.clear();return 1;}
  if(!collecting)return 0;
  if(input=='\n'){collecting=false;epoch=strtoul(value.c_str(),nullptr,10);if(value.empty()||!acceptable(epoch)){Serial.println("CLOCK preview=invalid");return 1;}return 2;}
  if(input!='\r'){if(input>='0'&&input<='9'&&value.size()<12)value+=input;else{collecting=false;Serial.println("CLOCK preview=invalid");}}return 1;
}
bool ReaderClock::selfTest(){
  bool ok=true;int cases=0;
  struct Case {int64_t epoch;int hour,minute,dst;};Case times[]={{1774569540,1,59,0},{1774569600,3,0,1},{1792882740,1,59,1},{1792882800,1,0,0}};
  for(auto v:times){time_t epoch=v.epoch;tm t{};localtime_r(&epoch,&t);ok&=t.tm_hour==v.hour&&t.tm_min==v.minute&&t.tm_isdst==v.dst;++cases;}
  for(uint64_t period:{hour,day,timeout}){ok&=!elapsed(123+period-1,123,period)&&elapsed(123+period,123,period);++cases;}
  ok&=1774569599/60!=1774569600/60&&dateText(tm{0,0,0,1,10,125})=="November 1, 2025";
  Serial.printf("SELFTEST clock cases=%d date/DST/schedules pass=%d\n",cases,ok);return ok;
}
#endif
