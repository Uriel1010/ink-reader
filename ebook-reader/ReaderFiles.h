#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <FS.h>
#include <functional>

class ReaderFiles {
public:
  void recover(bool cardReady);
  bool selfTest(bool cardReady);
  void start(bool cardReady);
  void poll();
  void stop();
  void setCancellationProbe(std::function<bool()> probe){cancelProbe=probe;}
  std::string execute(uint8_t op,uint32_t id,const std::string &body,uint16_t &status);
  bool active() const { return enabled; }
  bool ended() const { return ending; }
  bool connected() const { return enabled&&identified; }
  bool busy() const { return operation!=0; }
  uint32_t completed() const { return position; }
  uint32_t total() const { return length; }
  const std::string &filename() const { return target; }
  const std::string &message() const { return statusText; }
  bool changed() const { return modified; }
private:
  std::function<bool()> cancelProbe;
  std::string response;uint16_t responseStatus=0,responseStatusCached=0;
  bool enabled=false,ending=false,card=false,identified=false,modified=false;
  int operation=0;
  uint32_t lastActivity=0,lastTransfer=0,position=0,length=0,expectedCRC=0,runningCRC=~0u;
  uint32_t cachedId=0,cachedRequestCRC=0;
  uint8_t cachedOpcode=0;
  std::string target,temp,backup,statusText;
  File handle;
  std::vector<uint8_t> cachedReply;
  uint32_t lastChunkOffset=0,lastChunkCRC=0,lastChunkSize=0;
  void dispatch(uint8_t opcode,uint32_t requestId,uint32_t requestCRC,const uint8_t *payload,size_t size);
  void reply(uint8_t opcode,uint32_t requestId,uint16_t status,const uint8_t *payload,size_t size,bool remember=true);
  void jsonReply(uint8_t opcode,uint32_t requestId,uint16_t status,const std::string &text);
  void cancel();
};
