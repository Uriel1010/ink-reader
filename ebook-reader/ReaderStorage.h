#pragma once
#include <stdint.h>
#include <string>
#include <vector>
struct ReaderSettings {
  int fontPx=16, fontFamily=3, rotation=0, fullRefreshEvery=8, margin=20, lineGap=5;
  bool showTitle=true,showProgress=true,development=true,smoothText=false;
};
inline const char *fontFamilyKey(int family){return family==3?"screen_sans":family==1?"alef":family==2?"book_serif":"noto";}
struct NamedBookmark {std::string path,label,passage;uint32_t chapter=0,offset=0;};
struct Bookmark { uint32_t version=1,chapter=0,offset=0; };
class ReaderStorage {
public:
  ReaderSettings settings;
  void begin(bool cardReady,bool restoringDocuments=false);
  bool saveSettings();
  bool loadBookmark(const std::string &path,Bookmark &saved);
  bool saveBookmark(const std::string &path,const Bookmark &saved,uint32_t boundary=UINT32_MAX);
  uint32_t loadBoundary(const std::string &path,uint32_t fallback);
  void dumpFiles();
  bool selfTest();
  std::string lastBook() const;
  std::vector<NamedBookmark> namedBookmarks(const std::string &path="") const;
  bool toggleNamedBookmark(const NamedBookmark &mark);
  std::string loadDocument(const char *name);
  bool saveDocument(const char *name,const std::string &text);
  bool cardWritable() const { return writable; }
private:
  bool card=false,writable=false,bookmarksDirty=false;
  void *bookmarks=nullptr;
};
