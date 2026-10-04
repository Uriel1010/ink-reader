#include <Arduino.h>
#include <SD.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include "ReaderFiles.h"
#include "ReaderVersion.h"
#include "ReaderClock.h"
#include "ReaderCache.h"
#include "cJSON.h"

static uint8_t fileBlock[4096]; // Main-task-only scratch: avoid nested 4 KiB stack buffers.
static const char *journalPath="/.crowreader/transfer.json";
static uint32_t read32(const uint8_t *bytes){return uint32_t(bytes[0])|uint32_t(bytes[1])<<8|uint32_t(bytes[2])<<16|uint32_t(bytes[3])<<24;}
static void append32(std::vector<uint8_t> &bytes,uint32_t value){for(int shift=0;shift<32;shift+=8)bytes.push_back(value>>shift);}
static uint32_t crcStep(uint32_t crc,const uint8_t *bytes,size_t size){while(size--){crc^=*bytes++;for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xEDB88320u&-(crc&1));}return crc;}
static std::string lower(std::string text){for(char &character:text)if(character>='A'&&character<='Z')character+=32;return text;}
static bool safePath(const std::string &path,bool root=false){
  if(path.empty()||path.size()>240||path[0]!='/'||(!root&&path=="/")||(path.size()>1&&path.back()=='/'))return false;
  size_t start=1;while(start<path.size()){
    size_t end=path.find('/',start);if(end==std::string::npos)end=path.size();std::string component=lower(path.substr(start,end-start));
    if(component.empty()||component=="."||component==".."||component.front()=='.'||component.back()=='.'||component.back()==' '||component.find(".crupload")!=std::string::npos||component.find(".crbackup")!=std::string::npos)return false;
    for(unsigned char character:component)if(character<32||character=='\\'||character==':'||character=='*'||character=='?'||character=='"'||character=='<'||character=='>'||character=='|')return false;
    start=end+1;
  }return true;
}
static std::string stringField(cJSON *document,const char *name){cJSON *value=cJSON_GetObjectItemCaseSensitive(document,name);return cJSON_IsString(value)?value->valuestring:"";}
static bool numberField(cJSON *document,const char *name,uint32_t &output){cJSON *value=cJSON_GetObjectItemCaseSensitive(document,name);if(!cJSON_IsNumber(value)||value->valuedouble<0||value->valuedouble>4294967295.0||floor(value->valuedouble)!=value->valuedouble)return false;output=uint32_t(value->valuedouble);return true;}
static std::string encode(cJSON *document){char *text=cJSON_PrintUnformatted(document);std::string output=text?text:"{}";cJSON_free(text);return output;}
static bool verifiedFile(const std::string &path,uint32_t length,uint32_t expected,const std::function<void(uint32_t,uint32_t)> &progress={},const std::function<bool()> &abort={}){
  File file=SD.open(path.c_str(),FILE_READ);if(!file||file.isDirectory()||file.size()!=length){file.close();return false;}
  auto &block=fileBlock;uint32_t crc=~0u,lastReport=millis();size_t count=0;while(file.available()){if(abort&&abort()){file.close();return false;}size_t read=file.read(block,sizeof(block));if(!read)break;count+=read;crc=crcStep(crc,block,read);if(progress&&uint32_t(millis()-lastReport)>=2000){progress(count,length);lastReport=millis();}delay(1);}file.close();return count==length&&~crc==expected;
}
static bool writeJournal(const std::string &path,uint32_t length,uint32_t crc,bool ready){
  cJSON *document=cJSON_CreateObject();cJSON_AddStringToObject(document,"target",path.c_str());cJSON_AddNumberToObject(document,"size",length);cJSON_AddNumberToObject(document,"crc",crc);cJSON_AddBoolToObject(document,"ready",ready);
  std::string text=encode(document);cJSON_Delete(document);std::string pending=std::string(journalPath)+".tmp";
  SD.mkdir("/.crowreader");SD.remove(pending.c_str());File file=SD.open(pending.c_str(),FILE_WRITE);bool success=file&&file.write((const uint8_t *)text.data(),text.size())==text.size();file.flush();file.close();
  if(!success)return false;
  file=SD.open(pending.c_str(),FILE_READ);std::string check(file.size(),'\0');bool valid=file&&file.readBytes(&check[0],check.size())==check.size()&&check==text;file.close();if(!valid)return false;
  SD.remove(journalPath);return SD.rename(pending.c_str(),journalPath);
}
void ReaderFiles::recover(bool cardReady){
  if(!cardReady)return;
  File file=SD.open(journalPath,FILE_READ);if(!file){file=SD.open((std::string(journalPath)+".tmp").c_str(),FILE_READ);}
  if(!file)return;if(file.size()>2048){file.close();return;}
  std::string text(file.size(),'\0');file.readBytes(&text[0],text.size());file.close();cJSON *document=cJSON_Parse(text.c_str());std::string path=stringField(document,"target");uint32_t size=0,crc=0;
  bool valid=safePath(path)&&numberField(document,"size",size)&&numberField(document,"crc",crc);bool ready=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(document,"ready"));cJSON_Delete(document);
  if(!valid){Serial.println("TRANSFER recovery journal invalid; reserved files retained");return;}
  std::string pending=path+".crupload",previous=path+".crbackup";
  bool completed=ready&&verifiedFile(path,size,crc);
  if(!completed&&SD.exists(previous.c_str())){
    if(SD.exists(path.c_str())&&!SD.remove(path.c_str()))return;
    if(!SD.rename(previous.c_str(),path.c_str()))return;
  }
  if(completed&&SD.exists(previous.c_str())&&!SD.remove(previous.c_str()))return;
  if(SD.exists(pending.c_str())&&!SD.remove(pending.c_str()))return;
  SD.remove(journalPath);SD.remove((std::string(journalPath)+".tmp").c_str());Serial.printf("TRANSFER recovery committed=%d\n",completed);
}
void ReaderFiles::start(bool cardReady){
  recover(cardReady);card=cardReady;enabled=true;ending=false;identified=false;modified=false;operation=0;position=length=0;target.clear();cachedReply.clear();cachedId=0;lastActivity=lastTransfer=millis();statusText="Join the reader Wi-Fi";
}
bool ReaderFiles::selfTest(bool cardReady){
  const std::string path="/CrowReader transfer tests/stage-recovery.bin";
  if(!cardReady||enabled||SD.exists(journalPath)||SD.exists((std::string(journalPath)+".tmp").c_str())||SD.exists(path.c_str())||SD.exists((path+".crbackup").c_str())||SD.exists((path+".crupload").c_str()))return false;
  SD.mkdir("/CrowReader transfer tests");const uint8_t original[]={11,22,33,44},replacement[]={55,66,77,88};bool passed=true;
  auto write=[&](const std::string &name,const uint8_t *bytes){SD.remove(name.c_str());File file=SD.open(name.c_str(),FILE_WRITE);bool valid=file&&file.write(bytes,4)==4;file.flush();file.close();return valid;};
  for(int stage=0;stage<4;stage++){
    bool valid=write(path,original)&&write(path+".crupload",replacement)&&writeJournal(path,4,readerCRC(replacement,4),stage!=0);
    if(stage>0)valid=SD.rename(path.c_str(),(path+".crbackup").c_str())&&valid;
    if(stage==2)valid=SD.rename((path+".crupload").c_str(),path.c_str())&&valid;
    if(stage==3)valid=write(path,original)&&valid;
    recover(true);valid=verifiedFile(path,4,readerCRC(stage==2?replacement:original,4))&&!SD.exists((path+".crupload").c_str())&&!SD.exists((path+".crbackup").c_str())&&!SD.exists(journalPath)&&valid;
    Serial.printf("TRANSFER_TEST stage=%d pass=%d\n",stage,valid);passed=passed&&valid;SD.remove(path.c_str());
    if(!valid)break;
  }Serial.printf("SELFTEST transfer_recovery pass=%d\n",passed);return passed;
}
void ReaderFiles::cancel(){handle.close();if(operation==1){recover(card);if(SD.exists(temp.c_str()))SD.remove(temp.c_str());}operation=0;position=length=0;statusText="Transfer cancelled";}
void ReaderFiles::stop(){cancel();enabled=false;ending=true;}
void ReaderFiles::reply(uint8_t opcode,uint32_t requestId,uint16_t status,const uint8_t *payload,size_t size,bool remember){
  if(status==11)return;
  response.assign((const char*)payload,size);responseStatus=status;
  if(remember){cachedReply.assign(payload,payload+size);cachedId=requestId;cachedOpcode=opcode;responseStatusCached=status;}
}
std::string ReaderFiles::execute(uint8_t opcode,uint32_t id,const std::string &body,uint16_t &status){
  response.clear();responseStatus=0;
  if(opcode==15){lastActivity=millis();status=0;return "{}";}
  uint32_t crc=~crcStep(~0u,(const uint8_t*)body.data(),body.size());
  dispatch(opcode,id,crc,(const uint8_t*)body.data(),body.size());
  if(opcode!=13)lastActivity=millis();
  if(opcode==4||opcode==5||opcode==6||opcode==7||opcode==8)lastTransfer=millis();
  status=responseStatus;return response;
}
void ReaderFiles::jsonReply(uint8_t opcode,uint32_t requestId,uint16_t status,const std::string &text){reply(opcode,requestId,status,(const uint8_t *)text.data(),text.size());}
void ReaderFiles::dispatch(uint8_t opcode,uint32_t requestId,uint32_t requestCRC,const uint8_t *payload,size_t size){
  if(!cachedReply.empty()&&requestId==cachedId){if(opcode==cachedOpcode&&requestCRC==cachedRequestCRC){response.assign((const char*)cachedReply.data(),cachedReply.size());responseStatus=responseStatusCached;}else{response="{\"error\":\"Request ID reused with different content\"}";responseStatus=4;}return;}
  cachedRequestCRC=requestCRC;
  cJSON *document=nullptr;if(opcode!=5&&opcode!=8){std::string text((const char *)payload,size);document=cJSON_Parse(text.c_str());}
  std::string path=stringField(document,"path");
  auto error=[&](uint16_t code,const char *message){cJSON *response=cJSON_CreateObject();cJSON_AddStringToObject(response,"error",message);jsonReply(opcode,requestId,code,encode(response));cJSON_Delete(response);};
  auto checking=[&](uint32_t completed,uint32_t total){lastActivity=millis();std::string text="{\"checking\":true,\"done\":"+std::to_string(completed)+",\"total\":"+std::to_string(total)+"}";reply(opcode,requestId,11,(const uint8_t *)text.data(),text.size(),false);};
  if(opcode==1){identified=true;statusText="Browser connected";jsonReply(opcode,requestId,0,"{\"product\":\"CrowReader\",\"protocol\":1,\"chunk\":4096,\"firmware\":\"" INK_READER_VERSION "\"}");}
  else if(!identified)error(1,"Identify the reader first");
  else if(opcode==11){cancel();jsonReply(opcode,requestId,0,"{}");}
  else if(opcode==12){cancel();jsonReply(opcode,requestId,0,"{}");ending=true;}
  else if(opcode==13)jsonReply(opcode,requestId,0,"{}");
  else if(opcode==14){
#if READER_ENABLE_CLOCK_SCREENSAVER
    if(operation)error(3,"Finish the active transfer first");
    else if(!cJSON_IsObject(document))error(3,"Invalid clock configuration");
    else if(ReaderClock::configure(stringField(document,"ssid"),stringField(document,"password"),cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(document,"clear"))))jsonReply(opcode,requestId,0,"{}");
    else error(3,"Clock configuration is invalid or could not be stored");
#else
    error(3,"Clock screensaver is disabled in this build");
#endif
  }
  else if(!card)error(2,"microSD is not available");
  else if(opcode==3){cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"total",double(SD.totalBytes()));cJSON_AddNumberToObject(response,"free",double(SD.totalBytes()-SD.usedBytes()));jsonReply(opcode,requestId,0,encode(response));cJSON_Delete(response);}
  else if(opcode==5){
    if(operation!=1||size<8||size>4104)error(3,"No upload is active");
    else if(~crcStep(~0u,payload+8,size-8)!=read32(payload+4))error(6,"Chunk CRC mismatch");
    else if(lastChunkSize&&read32(payload)==lastChunkOffset&&size-8==lastChunkSize&&read32(payload+4)==lastChunkCRC)jsonReply(opcode,requestId,0,"{\"offset\":"+std::to_string(position)+"}");
    else if(read32(payload)!=position||size-8>length-position)error(4,"Unexpected upload offset");
    else if(handle.write(payload+8,size-8)!=size-8){cancel();error(5,"Card write failed");}
    else{lastChunkOffset=position;lastChunkSize=size-8;lastChunkCRC=read32(payload+4);runningCRC=crcStep(runningCRC,payload+8,size-8);position+=size-8;jsonReply(opcode,requestId,0,"{\"offset\":"+std::to_string(position)+"}");}
  }else if(opcode==6){
    if(operation!=1)error(3,"No upload is active");
    else{
      handle.flush();handle.close();bool valid=position==length&&~runningCRC==expectedCRC&&verifiedFile(temp,length,expectedCRC,checking,cancelProbe);
      if(!valid){cancel();error(6,"File length or CRC verification failed");}
      else if(!writeJournal(target,length,expectedCRC,true)){cancel();error(5,"Could not save replacement journal");}
      else{
        bool committed=true;if(SD.exists(target.c_str()))committed=SD.rename(target.c_str(),backup.c_str());
        if(committed)committed=SD.rename(temp.c_str(),target.c_str());
        if(!committed){recover(card);operation=0;error(5,"Replacement failed; original restored");}
        else{operation=0;bool verified=verifiedFile(target,length,expectedCRC,checking);modified=modified||verified;statusText=verified?"File saved":"Replacement verification failed";
          if(verified){bool cleaned=!SD.exists(backup.c_str())||SD.remove(backup.c_str());if(cleaned)cleaned=SD.remove(journalPath);SD.remove((std::string(journalPath)+".tmp").c_str());if(cleaned)jsonReply(opcode,requestId,0,"{}");else error(5,"File saved; reconnect to finish recovery cleanup");}
          else{recover(card);error(5,"Replacement verification failed; previous file restored");}}
      }
    }
  }else if(opcode==8){
    if(operation!=2||size!=8)error(3,"No download is active");
    else{uint32_t offset=read32(payload),count=read32(payload+4);if(offset>length||count>4096||!handle.seek(offset))error(4,"Invalid download range");
      else{count=std::min(count,length-offset);std::vector<uint8_t> bytes;append32(bytes,offset);bytes.resize(4+count);if(handle.read(bytes.data()+4,count)!=count)error(5,"Card read failed");else{position=offset+count;reply(opcode,requestId,0,bytes.data(),bytes.size());}}}
  }else if(operation)error(3,"Finish or cancel the active transfer");
  else if(opcode==2){
    if(!safePath(path,true))error(7,"Invalid or protected path");else{
      uint32_t cursor=0;numberField(document,"cursor",cursor);File directory=SD.open(path.c_str(),FILE_READ);
      if(!directory||!directory.isDirectory())error(8,"Folder not found");else{
        cJSON *response=cJSON_CreateObject(),*entries=cJSON_AddArrayToObject(response,"entries");uint32_t index=0,count=0;bool more=false;File entry;
        while((entry=directory.openNextFile())){
          std::string name=entry.name();size_t slash=name.find_last_of('/');if(slash!=std::string::npos)name=name.substr(slash+1);std::string full=path=="/"?"/"+name:path+"/"+name;
          if(safePath(full)){if(index++>=cursor){if(count>=8){more=true;entry.close();break;}cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",name.c_str());cJSON_AddBoolToObject(item,"directory",entry.isDirectory());cJSON_AddNumberToObject(item,"size",double(entry.size()));cJSON_AddItemToArray(entries,item);++count;}}entry.close();
        }
        cJSON_AddNumberToObject(response,"next",more?double(cursor+count):-1);jsonReply(opcode,requestId,0,encode(response));cJSON_Delete(response);
      }directory.close();
    }
  }else if(!safePath(path))error(7,"Invalid or protected path");
  else if(opcode==4){
    uint32_t sizeValue=0,crcValue=0;bool overwrite=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(document,"overwrite"));
    File old=SD.open(path.c_str(),FILE_READ);bool directory=old&&old.isDirectory();old.close();
    std::string parent=path.substr(0,path.find_last_of('/'));if(parent.empty())parent="/";File folder=SD.open(parent.c_str(),FILE_READ);bool parentExists=folder&&folder.isDirectory();folder.close();
    if(!numberField(document,"size",sizeValue)||!numberField(document,"crc",crcValue)||sizeValue>512u*1024u*1024u)error(4,"Invalid file size or CRC");
    else if(!parentExists||directory)error(8,"Destination folder missing or path is a folder");
    else if(SD.exists(path.c_str())&&!overwrite)error(9,"File already exists");
    else if(uint64_t(sizeValue)+65536>SD.totalBytes()-SD.usedBytes())error(10,"Not enough free space");
    else if(SD.exists((path+".crbackup").c_str())||SD.exists(journalPath))error(5,"Unresolved previous transaction");
    else if(!writeJournal(path,sizeValue,crcValue,false))error(5,"Could not create upload journal");
    else{target=path;temp=path+".crupload";backup=path+".crbackup";SD.remove(temp.c_str());handle=SD.open(temp.c_str(),FILE_WRITE);
      if(!handle){recover(card);error(5,"Could not create upload file");}else{operation=1;position=0;lastChunkSize=0;length=sizeValue;expectedCRC=crcValue;runningCRC=~0u;statusText="Receiving file";jsonReply(opcode,requestId,0,"{}");}}
  }else if(opcode==7){
    handle=SD.open(path.c_str(),FILE_READ);if(!handle||handle.isDirectory()){handle.close();error(8,"File not found");}else{
      target=path;length=handle.size();position=0;operation=2;statusText="Sending file";auto &block=fileBlock;uint32_t crc=~0u,lastReport=millis();while(handle.available()){if(cancelProbe&&cancelProbe()){cancel();error(3,"Download cancelled");cJSON_Delete(document);return;}size_t read=handle.read(block,sizeof(block));if(!read)break;crc=crcStep(crc,block,read);if(uint32_t(millis()-lastReport)>=2000){checking(handle.position(),length);lastReport=millis();}delay(1);}handle.seek(0);
      jsonReply(opcode,requestId,0,"{\"size\":"+std::to_string(length)+",\"crc\":"+std::to_string(~crc)+"}");}
  }else if(opcode==9){if(SD.exists(path.c_str()))error(9,"Path already exists");else if(!SD.mkdir(path.c_str()))error(5,"Could not create folder");else{modified=true;jsonReply(opcode,requestId,0,"{}");}}
  else if(opcode==10){File item=SD.open(path.c_str(),FILE_READ);bool exists=bool(item),directory=item&&item.isDirectory();item.close();if(!exists)error(8,"Path not found");else if(!(directory?SD.rmdir(path.c_str()):SD.remove(path.c_str())))error(5,"Could not delete; folders must be empty");else{modified=true;jsonReply(opcode,requestId,0,"{}");}}
  else error(4,"Unknown operation");cJSON_Delete(document);
}
void ReaderFiles::poll(){
  if(!enabled)return;
  uint32_t now=millis();
  if(operation&&uint32_t(now-lastTransfer)>=30000){cancel();lastActivity=now;}
  if(!operation&&uint32_t(now-lastTransfer)>=300000){statusText="Wi-Fi session timed out";ending=true;}
}
