#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include "cJSON.h"
#include "miniz.h"
#include "ReaderStorage.h"
#include <vector>
#include <math.h>
static const char *folder="/.crowreader";
static const char *settingsPath="/.crowreader/settings.json";
static const char *bookmarksPath="/.crowreader/bookmarks.json";
static uint32_t hashPath(const std::string &s){uint32_t h=2166136261u;for(uint8_t c:s){h^=c;h*=16777619u;}return h;}
static std::string nvsKey(const std::string &path){char key[12];snprintf(key,sizeof(key),"b%08lx",(unsigned long)hashPath(path));return key;}
static std::string faultKey(const std::string &path){std::string key=nvsKey(path);key[0]='e';return key;}
static cJSON *readJSON(const std::string &path,size_t limit){
  File f=SD.open(path.c_str(),FILE_READ);if(!f)return nullptr;
  size_t size=f.size();if(!size||size>limit){f.close();return nullptr;}
  std::vector<char> text(size+1,0);size_t count=f.readBytes(text.data(),size);f.close();
  if(count!=size)return nullptr;
  cJSON *doc=cJSON_ParseWithLengthOpts(text.data(),size+1,nullptr,true);
  if(!cJSON_IsObject(doc)){cJSON_Delete(doc);return nullptr;}return doc;
}
static cJSON *readRecoverable(const char *path,size_t limit){
  cJSON *doc=readJSON(path,limit);
  if(!doc){doc=readJSON(std::string(path)+".bak",limit);if(doc)Serial.printf("STORAGE recovered=%s\n",path);}
  return doc;
}
static bool writeJSON(const char *path,cJSON *doc){
  char *text=cJSON_Print(doc);if(!text)return false;
  std::string temp=std::string(path)+".tmp",backup=std::string(path)+".bak";
  if(SD.exists(temp.c_str())&&!SD.remove(temp.c_str())){cJSON_free(text);return false;}
  File f=SD.open(temp.c_str(),FILE_WRITE);if(!f){cJSON_free(text);return false;}
  size_t length=strlen(text);bool ok=f.write((const uint8_t *)text,length)==length;
  f.flush();f.close();cJSON_free(text);
  // Re-read before replacing the last good copy.
  cJSON *check=ok?readJSON(temp,256*1024):nullptr;ok=check!=nullptr;cJSON_Delete(check);
  if(!ok){SD.remove(temp.c_str());return false;}
  if(SD.exists(path)){
    if(SD.exists(backup.c_str())&&!SD.remove(backup.c_str()))return false;
    if(!SD.rename(path,backup.c_str()))return false;
  }
  if(!SD.rename(temp.c_str(),path)){
    if(!SD.exists(path)&&SD.exists(backup.c_str()))SD.rename(backup.c_str(),path);
    return false;
  }
  Serial.printf("STORAGE saved=%s bytes=%u\n",path,unsigned(length));return true;
}
static int number(cJSON *object,const char *key,int fallback){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(object,key);
  return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&floor(v->valuedouble)==v->valuedouble&&v->valuedouble>=0&&v->valuedouble<=2147483647?v->valueint:fallback;
}
static std::string readNamedBackup(Preferences &p){
  size_t length=p.getBytesLength("namedzip");if(length<4||length>16384)return p.getString("named","").c_str();
  std::vector<uint8_t> packed(length);if(p.getBytes("namedzip",packed.data(),length)!=length)return "";uint32_t size=0;memcpy(&size,packed.data(),4);if(!size||size>65536)return "";
  std::string text(size,'\0');mz_ulong count=size;if(mz_uncompress((unsigned char *)&text[0],&count,packed.data()+4,length-4)!=MZ_OK||count!=size)return "";return text;
}
static bool writeNamedBackup(Preferences &p,const char *text){
  size_t length=strlen(text);if(!length||length>65536)return false;mz_ulong count=mz_compressBound(length);std::vector<uint8_t> packed(count+4);
  if(mz_compress2(packed.data()+4,&count,(const unsigned char *)text,length,6)!=MZ_OK||count+4>16384)return false;
  uint32_t size=length;memcpy(packed.data(),&size,4);return p.putBytes("namedzip",packed.data(),count+4)==count+4;
}
void ReaderStorage::begin(bool cardReady,bool restoringDocuments){
  cJSON_Delete((cJSON *)bookmarks);bookmarks=nullptr;
  card=cardReady;
  Preferences p;p.begin("crowreader",true);
  const int oldFonts[]={14,16,18,20};
  settings.fontPx=p.getUChar("fontpx",oldFonts[std::min(3,int(p.getUChar("font",1)))]);
  settings.fontFamily=p.getUChar("family",3);
  settings.smoothText=false; // Ignore retired grayscale preferences.
  settings.rotation=p.getUShort("rotation",0);settings.fullRefreshEvery=p.getUChar("cleanup",8);
  settings.margin=p.getUChar("margin",20);settings.lineGap=p.getUChar("gap",5);
  settings.showTitle=p.getBool("title",true);settings.showProgress=p.getBool("progress",true);settings.development=p.getBool("dev",true);
  bool settingsFailed=p.getBool("cfgfailed",false),namedFailed=p.getBool("namedfailed",false);
  if(restoringDocuments){settingsFailed=false;namedFailed=false;}
  std::string namedBackup=readNamedBackup(p),lastBackup=p.getString("lastbook","").c_str();p.end();
  if(card){
    writable=SD.exists(folder)||SD.mkdir(folder);
    cJSON *doc=readRecoverable(settingsPath,4096);
    if(doc&&!settingsFailed){settings.fontPx=number(doc,"font_px",settings.fontPx);settings.rotation=number(doc,"rotation",settings.rotation);settings.fullRefreshEvery=number(doc,"full_refresh_every",settings.fullRefreshEvery);}
    if(doc&&!settingsFailed){
      cJSON *family=cJSON_GetObjectItemCaseSensitive(doc,"font_family");
      if(cJSON_IsString(family))settings.fontFamily=std::string(family->valuestring)=="screen_sans"?3:std::string(family->valuestring)=="alef"?1:std::string(family->valuestring)=="noto"?0:2;
      settings.margin=number(doc,"margin",20);settings.lineGap=number(doc,"line_gap",5);
      auto flag=[&](const char *key,bool fallback){cJSON *v=cJSON_GetObjectItemCaseSensitive(doc,key);return cJSON_IsBool(v)?bool(cJSON_IsTrue(v)):fallback;};
      settings.showTitle=flag("show_title",true);settings.showProgress=flag("show_progress",true);settings.development=flag("development_mode",true);
      settings.smoothText=false;
    }
    cJSON_Delete(doc);
    bookmarks=readRecoverable(bookmarksPath,256*1024);
  }
  if(settings.fontPx!=14&&settings.fontPx!=16&&settings.fontPx!=18&&settings.fontPx!=20)settings.fontPx=16;
  if(settings.fontFamily<0||settings.fontFamily>3)settings.fontFamily=2;
  if(settings.rotation!=0&&settings.rotation!=90&&settings.rotation!=180&&settings.rotation!=270)settings.rotation=0;
  if(settings.fullRefreshEvery!=0&&settings.fullRefreshEvery!=4&&settings.fullRefreshEvery!=8&&settings.fullRefreshEvery!=16)settings.fullRefreshEvery=8;
  if(settings.margin!=12&&settings.margin!=20&&settings.margin!=28)settings.margin=20;
  if(settings.lineGap!=3&&settings.lineGap!=5&&settings.lineGap!=8)settings.lineGap=5;
  if(!bookmarks)bookmarks=cJSON_CreateObject();
  if(bookmarks&&!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"books"))){
    cJSON_DeleteItemFromObjectCaseSensitive((cJSON *)bookmarks,"books");
    cJSON_AddObjectToObject((cJSON *)bookmarks,"books");
  }
  if(bookmarks){
    cJSON *root=(cJSON *)bookmarks;
    if(namedFailed||!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"named"))){cJSON *named=cJSON_Parse(namedBackup.c_str());if(cJSON_IsArray(named)){cJSON_DeleteItemFromObjectCaseSensitive(root,"named");cJSON_AddItemToObject(root,"named",named);}else cJSON_Delete(named);}
    if(!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root,"last_book"))&&!lastBackup.empty())cJSON_AddStringToObject(root,"last_book",lastBackup.c_str());
    if(namedFailed)bookmarksDirty=true;
  }
  if(card&&writable&&(!SD.exists(settingsPath)||settingsFailed))saveSettings();
  if(restoringDocuments&&bookmarks){
    // Verified restored SD documents are authoritative over stale failure fallbacks.
    p.begin("crowreader",false);p.putBool("cfgfailed",false);p.putBool("namedfailed",false);
    cJSON *entry=nullptr,*root=(cJSON *)bookmarks;
    cJSON_ArrayForEach(entry,cJSON_GetObjectItemCaseSensitive(root,"books")){
      if(!entry->string)continue;int ch=number(entry,"chapter",-1),off=number(entry,"offset",-1);if(ch<0||off<0)continue;
      std::string path=entry->string;Bookmark mark{1,uint32_t(ch),uint32_t(off)};p.putBytes(nvsKey(path).c_str(),&mark,sizeof(mark));p.putBool(faultKey(path).c_str(),false);
      std::string key=nvsKey(path);key[0]='p';int boundary=number(entry,"boundary",-1);p.putUInt(key.c_str(),boundary<0?UINT32_MAX:uint32_t(boundary));
    }
    cJSON *last=cJSON_GetObjectItemCaseSensitive(root,"last_book");if(cJSON_IsString(last))p.putString("lastbook",last->valuestring);
    char *named=cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(root,"named"));if(named){writeNamedBackup(p,named);cJSON_free(named);}p.end();
  }
  Serial.printf("SETTINGS font=%d rotation=%d cleanup=%d card=%d\n",settings.fontPx,settings.rotation,settings.fullRefreshEvery,writable);
}
bool ReaderStorage::saveSettings(){
  Preferences p;p.begin("crowreader",false);
  p.putUChar("fontpx",settings.fontPx);p.putUShort("rotation",settings.rotation);p.putUChar("cleanup",settings.fullRefreshEvery);
  p.putUChar("family",settings.fontFamily);
  p.putBool("smooth",settings.smoothText);
  p.putUChar("margin",settings.margin);p.putUChar("gap",settings.lineGap);p.putBool("title",settings.showTitle);p.putBool("progress",settings.showProgress);p.putBool("dev",settings.development);p.end();
  cJSON *doc=card?readRecoverable(settingsPath,4096):nullptr;
  if(!doc)doc=cJSON_CreateObject();if(!doc)return false;
  for(const char *key:{"version","font_px","rotation","full_refresh_every","margin","line_gap","show_title","show_progress","development_mode"})cJSON_DeleteItemFromObjectCaseSensitive(doc,key);
  cJSON_AddNumberToObject(doc,"version",1);cJSON_AddNumberToObject(doc,"font_px",settings.fontPx);
  cJSON_DeleteItemFromObjectCaseSensitive(doc,"font_family");cJSON_AddStringToObject(doc,"font_family",fontFamilyKey(settings.fontFamily));
  cJSON_AddNumberToObject(doc,"rotation",settings.rotation);cJSON_AddNumberToObject(doc,"full_refresh_every",settings.fullRefreshEvery);
  cJSON_AddNumberToObject(doc,"margin",settings.margin);cJSON_AddNumberToObject(doc,"line_gap",settings.lineGap);
  cJSON_AddBoolToObject(doc,"show_title",settings.showTitle);cJSON_AddBoolToObject(doc,"show_progress",settings.showProgress);cJSON_AddBoolToObject(doc,"development_mode",settings.development);
  cJSON_DeleteItemFromObjectCaseSensitive(doc,"text_smoothing");cJSON_AddBoolToObject(doc,"text_smoothing",settings.smoothText);
  bool ok=card&&writeJSON(settingsPath,doc);cJSON_Delete(doc);writable=ok;
  p.begin("crowreader",false);p.putBool("cfgfailed",!ok);p.end();
  if(card&&!ok)Serial.println("STORAGE settings SD save failed; NVS backup saved");
  return ok;
}
bool ReaderStorage::loadBookmark(const std::string &path,Bookmark &saved){
  Preferences p;p.begin("crowreader",true);
  Bookmark deviceSaved{};size_t count=p.getBytes(nvsKey(path).c_str(),&deviceSaved,sizeof(deviceSaved));
  bool failed=p.getBool(faultKey(path).c_str(),false);p.end();
  bool deviceValid=count==sizeof(deviceSaved)&&deviceSaved.version==1;
  if(failed&&deviceValid){saved=deviceSaved;return true;}
  cJSON *books=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"books");
  cJSON *entry=cJSON_GetObjectItemCaseSensitive(books,path.c_str());
  int chapter=number(entry,"chapter",-1),offset=number(entry,"offset",-1);
  if(chapter>=0&&offset>=0){saved={1,uint32_t(chapter),uint32_t(offset)};return true;}
  if(deviceValid)saved=deviceSaved;return deviceValid;
}
uint32_t ReaderStorage::loadBoundary(const std::string &path,uint32_t fallback){
  Preferences p;p.begin("crowreader",true);std::string key=nvsKey(path);key[0]='p';bool failed=p.getBool(faultKey(path).c_str(),false);uint32_t device=p.getUInt(key.c_str(),fallback);p.end();if(failed)return device;
  cJSON *entry=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"books"),path.c_str()),*value=cJSON_GetObjectItemCaseSensitive(entry,"boundary");
  if(cJSON_IsNumber(value)&&value->valuedouble==-1)return UINT32_MAX;
  int boundary=number(entry,"boundary",-1);return boundary>=0?uint32_t(boundary):fallback;
}
bool ReaderStorage::saveBookmark(const std::string &path,const Bookmark &saved,uint32_t boundary){
  cJSON *books=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"books");
  cJSON *entry=cJSON_GetObjectItemCaseSensitive(books,path.c_str());
  cJSON *last=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"last_book");
  bool sameLast=cJSON_IsString(last)&&path==last->valuestring;
  bool unchanged=number(entry,"chapter",-1)==int(saved.chapter)&&number(entry,"offset",-1)==int(saved.offset);
  // Keep the previous firmware's NVS format as a fallback and migrate it lazily.
  Preferences p;p.begin("crowreader",false);Bookmark old{};p.getBytes(nvsKey(path).c_str(),&old,sizeof(old));
  if(old.version!=1||old.chapter!=saved.chapter||old.offset!=saved.offset)p.putBytes(nvsKey(path).c_str(),&saved,sizeof(saved));
  if(p.getString("lastbook","")!=path.c_str())p.putString("lastbook",path.c_str());p.end();
  std::string pivotKey=nvsKey(path);pivotKey[0]='p';p.begin("crowreader",false);if(p.getUInt(pivotKey.c_str(),UINT32_MAX)!=boundary)p.putUInt(pivotKey.c_str(),boundary);p.end();
  cJSON *previousBoundary=cJSON_GetObjectItemCaseSensitive(entry,"boundary");bool sameBoundary=cJSON_IsNumber(previousBoundary)&&previousBoundary->valuedouble==(boundary==UINT32_MAX?-1:double(boundary));
  if(unchanged&&sameLast&&sameBoundary&&!bookmarksDirty)return true;
  if(!unchanged||!sameBoundary){
    if(!cJSON_IsObject(entry)){cJSON *value=cJSON_CreateObject();if(!value)return false;if(entry)cJSON_ReplaceItemInObjectCaseSensitive(books,path.c_str(),value);else cJSON_AddItemToObject(books,path.c_str(),value);entry=value;}
    cJSON_DeleteItemFromObjectCaseSensitive(entry,"chapter");cJSON_DeleteItemFromObjectCaseSensitive(entry,"offset");
    cJSON_AddNumberToObject(entry,"chapter",saved.chapter);cJSON_AddNumberToObject(entry,"offset",saved.offset);
    cJSON_DeleteItemFromObjectCaseSensitive(entry,"boundary");cJSON_AddNumberToObject(entry,"boundary",boundary==UINT32_MAX?-1:double(boundary));
  }
  bookmarksDirty=true;
  cJSON_DeleteItemFromObjectCaseSensitive((cJSON *)bookmarks,"last_book");
  cJSON_AddStringToObject((cJSON *)bookmarks,"last_book",path.c_str());
  cJSON *version=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"version");
  if(!version)cJSON_AddNumberToObject((cJSON *)bookmarks,"version",1);
  bool ok=card&&writeJSON(bookmarksPath,(cJSON *)bookmarks);
  p.begin("crowreader",false);
  if(ok){
    bookmarksDirty=false;p.putBool("namedfailed",false);
    cJSON *item=nullptr;
    cJSON_ArrayForEach(item,books){if(item->string&&p.getBool(faultKey(item->string).c_str(),false))p.putBool(faultKey(item->string).c_str(),false);}
  }else p.putBool(faultKey(path).c_str(),true);
  p.end();
  if(card&&!ok){writable=false;Serial.println("STORAGE bookmark SD save failed; NVS backup saved");}
  Serial.printf("BOOKMARK chapter=%u offset=%u sd=%d\n",saved.chapter,saved.offset,ok);
  return ok;
}
std::string ReaderStorage::lastBook() const {
  cJSON *v=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"last_book");
  return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";
}
std::vector<NamedBookmark> ReaderStorage::namedBookmarks(const std::string &path) const{
  std::vector<NamedBookmark> result;cJSON *array=cJSON_GetObjectItemCaseSensitive((cJSON *)bookmarks,"named"),*entry=nullptr;
  cJSON_ArrayForEach(entry,array){
    cJSON *p=cJSON_GetObjectItemCaseSensitive(entry,"path"),*label=cJSON_GetObjectItemCaseSensitive(entry,"label"),*passage=cJSON_GetObjectItemCaseSensitive(entry,"passage");
    int ch=number(entry,"chapter",-1),offset=number(entry,"offset",-1);
    if(cJSON_IsString(p)&&p->valuestring&&ch>=0&&offset>=0&&(path.empty()||path==p->valuestring))
      result.push_back({p->valuestring,cJSON_IsString(label)?label->valuestring:"Bookmark",cJSON_IsString(passage)?passage->valuestring:"",uint32_t(ch),uint32_t(offset)});
  }return result;
}
bool ReaderStorage::toggleNamedBookmark(const NamedBookmark &mark){
  cJSON *root=(cJSON *)bookmarks,*array=cJSON_GetObjectItemCaseSensitive(root,"named");
  if(!cJSON_IsArray(array)){cJSON_DeleteItemFromObjectCaseSensitive(root,"named");array=cJSON_AddArrayToObject(root,"named");}
  if(!array)return false;
  int found=-1,index=0;cJSON *entry=nullptr;
  cJSON_ArrayForEach(entry,array){cJSON *path=cJSON_GetObjectItemCaseSensitive(entry,"path");if(cJSON_IsString(path)&&mark.path==path->valuestring&&number(entry,"chapter",-1)==int(mark.chapter)&&number(entry,"offset",-1)==int(mark.offset))found=index;++index;}
  if(found>=0)cJSON_DeleteItemFromArray(array,found);
  else{
    if(cJSON_GetArraySize(array)>=64)return false;
    cJSON *item=cJSON_CreateObject();if(!item)return false;
    cJSON_AddStringToObject(item,"path",mark.path.c_str());cJSON_AddStringToObject(item,"label",mark.label.c_str());cJSON_AddStringToObject(item,"passage",mark.passage.c_str());
    cJSON_AddNumberToObject(item,"chapter",mark.chapter);cJSON_AddNumberToObject(item,"offset",mark.offset);cJSON_AddItemToArray(array,item);
  }
  bool ok=card&&writeJSON(bookmarksPath,root);
  char *fallback=cJSON_PrintUnformatted(array);
  bool backed=false;
  if(fallback){Preferences p;p.begin("crowreader",false);backed=writeNamedBackup(p,fallback);if(ok||backed)p.putBool("namedfailed",!ok);p.end();cJSON_free(fallback);}if(!ok)bookmarksDirty=true;
  Serial.printf("NAMED saved_sd=%d saved_device=%d\n",ok,backed);return ok||backed;
}
std::string ReaderStorage::loadDocument(const char *name){
  if(!name||strchr(name,'/')||strstr(name,".."))return "";
  if(!strcmp(name,"session.json")){Preferences p;p.begin("crowreader",true);bool failed=p.getBool("sessionfailed",false);String backup=p.getString("session","");p.end();if(failed&&backup.length())return backup.c_str();}
  std::string path=std::string(folder)+"/"+name;cJSON *doc=card?readRecoverable(path.c_str(),128*1024):nullptr;
  if(!doc&&!strcmp(name,"session.json")){Preferences p;p.begin("crowreader",true);String text=p.getString("session","");p.end();return text.c_str();}
  if(!doc)return "";char *text=cJSON_PrintUnformatted(doc);std::string result=text?text:"";cJSON_free(text);cJSON_Delete(doc);return result;
}
bool ReaderStorage::saveDocument(const char *name,const std::string &text){
  if(!name||strchr(name,'/')||strstr(name,".."))return false;
  cJSON *doc=cJSON_Parse(text.c_str());if(!cJSON_IsObject(doc)){cJSON_Delete(doc);return false;}
  std::string path=std::string(folder)+"/"+name;bool ok=card&&writeJSON(path.c_str(),doc);cJSON_Delete(doc);
  if(!strcmp(name,"session.json")){Preferences p;p.begin("crowreader",false);if(p.getString("session","")!=text.c_str())p.putString("session",text.c_str());p.putBool("sessionfailed",!ok);p.end();}
  return ok;
}
void ReaderStorage::dumpFiles(){
  if(!card){Serial.println("STORAGE no SD");return;}
  for(const char *path:{settingsPath,bookmarksPath}){
    Serial.printf("JSON_BEGIN %s\n",path);File f=SD.open(path,FILE_READ);
    if(f){while(f.available())Serial.write(f.read());f.close();}
    Serial.println("\nJSON_END");
  }
}

// Exercise production recovery helpers on disposable files, never user preferences.
bool ReaderStorage::selfTest(){
  if(!card)return false;const char *path="/.crowreader/validation.json";
  for(const char *suffix:{"",".bak",".tmp"})SD.remove((std::string(path)+suffix).c_str());
  cJSON *missing=readRecoverable(path,4096);bool ok=missing==nullptr;cJSON_Delete(missing);
  cJSON *a=cJSON_Parse("{\"version\":1,\"offset\":1677,\"extension\":true}");ok=writeJSON(path,a)&&ok;cJSON_Delete(a);
  cJSON *b=cJSON_Parse("{\"version\":2,\"offset\":4169,\"extension\":true}");ok=writeJSON(path,b)&&ok;cJSON_Delete(b);
  SD.remove(path);File broken=SD.open(path,FILE_WRITE);broken.print("{interrupted");broken.close();
  File pending=SD.open((std::string(path)+".tmp").c_str(),FILE_WRITE);pending.print("{unfinished");pending.close();
  cJSON *recovered=readRecoverable(path,4096);ok=ok&&number(recovered,"offset",-1)==1677&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(recovered,"extension"));cJSON_Delete(recovered);
  for(const char *suffix:{"",".bak",".tmp"})SD.remove((std::string(path)+suffix).c_str());
  Serial.printf("SELFTEST storage missing/corrupt/interrupted/previous_copy pass=%d\n",ok);return ok;
}
