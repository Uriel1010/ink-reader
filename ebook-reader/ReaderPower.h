#pragma once
#include <stdint.h>
#include <string>
class ReaderStorage;
bool readerWokeFromSleep();
uint8_t readerWakeButton();
void readerReleaseSleepHolds();
bool readerRestoreFrame(uint8_t *frame,unsigned &cleanup);
std::string readerSleepSession(ReaderStorage &storage);
bool readerEnterSleep(ReaderStorage &storage,const uint8_t *frame,const std::string &session,unsigned cleanup,void (*shutdown)(),uint32_t timerSeconds=0,uint8_t injectedButton=255,bool (*ready)()=nullptr);
