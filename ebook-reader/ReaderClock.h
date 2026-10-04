#pragma once
#include "ReaderFeatures.h"
#include <stdint.h>
#include <string>
class ReaderClock {
public:
  using Pixel=void (*)(int,int);
#if READER_ENABLE_CLOCK_SCREENSAVER
  void begin();
  void enter(bool synchronize=true);
  void leave();
  void retry();
  bool tick();
  void draw(int width,int height,Pixel pixel) const;
  bool cleanupDue() const;
  void presented(bool full);
  bool active() const {return running;}
  bool valid() const {return trusted||previewing;}
  bool syncing() const {return phase!=0;}
  bool preview() const {return previewing;}
  bool radioOn() const;
  int64_t minute() const;
  void previewAt(int64_t epoch);
  static bool configure(const std::string &ssid,const std::string &password,bool clear);
  static bool selfTest();
  static int previewCharacter(char input,int64_t &epoch);
private:
  bool running=false,trusted=false,previewing=false;
  int phase=0,lastStatus=-1;
  int64_t drawnMinute=-1,previewEpoch=0;
  uint64_t started=0,lastAttempt=0,lastCleanup=0,previewStarted=0;
  void startSync();
  void stopSync();
  int64_t now() const;
#else
  void begin(){} void enter(bool=true){} void leave(){} void retry(){}
  bool tick(){return false;} void draw(int,int,Pixel) const{}
  bool cleanupDue() const{return false;} void presented(bool){}
  bool active() const{return false;} bool valid() const{return false;}
  bool syncing() const{return false;} bool preview() const{return false;}
  bool radioOn() const{return false;} int64_t minute() const{return -1;}
  void previewAt(int64_t){}
  static bool configure(const std::string&,const std::string&,bool){return false;}
  static bool selfTest(){return true;}
  static int previewCharacter(char,int64_t&){return 0;}
#endif
};
