#include <Arduino.h>
#include <SD.h>
#include "ReaderValidation.h"
bool readerValidationBegin(ReaderStorage &storage,const std::string &session){
  if(!storage.cardWritable()||!storage.loadDocument("qa-active.json").empty())return false;
  std::string settings=storage.loadDocument("settings.json"),marks=storage.loadDocument("bookmarks.json");
  if(settings.empty()||marks.empty()||session.empty())return false;
  return storage.saveDocument("qa-settings.json",settings)&&storage.saveDocument("qa-bookmarks.json",marks)&&storage.saveDocument("qa-session.json",session)&&storage.saveDocument("qa-active.json","{\"version\":1}");
}
bool readerValidationRestore(ReaderStorage &storage,std::string &session){
  if(storage.loadDocument("qa-active.json").empty())return false;
  std::string settings=storage.loadDocument("qa-settings.json"),marks=storage.loadDocument("qa-bookmarks.json");session=storage.loadDocument("qa-session.json");
  if(settings.empty()||marks.empty()||session.empty())return false;
  if(!storage.saveDocument("settings.json",settings)||!storage.saveDocument("bookmarks.json",marks)||!storage.saveDocument("session.json",session))return false;
  storage.begin(true,true);storage.saveSettings();
  return true;
}
void readerValidationComplete(){
  for(const char *name:{"qa-active.json","qa-settings.json","qa-bookmarks.json","qa-session.json"})
    for(const char *suffix:{"",".tmp",".bak"})SD.remove((std::string("/.crowreader/")+name+suffix).c_str());
}
