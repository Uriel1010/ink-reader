#include <Arduino.h>
// miniz uses an approximately 12 KB local decompression context.
SET_LOOP_TASK_STACK_SIZE(32768);
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "Epub.h"
#include "BookImages.h"
#include "ReaderStorage.h"
#include "ReaderNavigation.h"
#include "ReaderText.h"
#include "ReaderValidation.h"
#include "ReaderVersion.h"
#include "ReaderClock.h"
#include "ReaderFontLab.h"
#include "ReaderGrayProbe.h"
#include "ReaderCache.h"
#include "ReaderPower.h"
#include "ReaderTransfer.h"
#include "ReaderTransferQR.h"
#include "ReaderGallery.h"
#include "cJSON.h"
#include "ImageFixtures.h"
#include "EpubFixtures.h"
#include "ReaderPdfFixture.h"
#include <unordered_map>
extern "C" {
#include "minibidi.h"
}
#include "EPD.h"
#include "EPD_GUI.h"
#include "ReaderFonts.h"
#include "ReaderFontFamilies.h"
#include "ReaderUIFonts.h"
#include "ReaderScreenFonts.h"
extern "C" {
#include "minibidi.h"
}

static uint8_t frame[15000];
static uint8_t *lastPresented=nullptr;
static bool havePresented=false,forceFullRefresh=false;
static bool sessionFrameCompatible=false;
// Portrait rows need 38 bytes each, including four padding bits.
static uint8_t canvas[15200];
static ReaderStorage storage;
static bool inSettings=false;
static int settingsRow=0;
static bool advancedSettings=false;
static const char *fontNames[]={"Noto Medium","Alef","Book serif","Ink bitmap"};
static bool lastDisplayWasSmooth=false;
static uint32_t settingsAnchor=0;
static int viewW(){return storage.settings.rotation%180?300:400;}
static int viewH(){return storage.settings.rotation%180?400:300;}
static SPIClass cardSPI(HSPI);
static QueueHandle_t inputQueue;
static TaskHandle_t inputTask=nullptr;
static const uint8_t buttonPins[] = {2,1,4,6,5};
static const int fontSizes[] = {14,16,18,20};
static int fontIndex = 1, selected = 0, chapter = 0, page = 0;
static bool inBook = false, dirty = true, sdReady = false, demo = false;
static uint32_t changedAt = 0;
static uint32_t forcedBoundary=UINT32_MAX;
static std::vector<std::string> books;
static std::unique_ptr<Epub> book;
static std::string chapterText, errorText;
struct Line { uint32_t start, end; bool rtl; int image=-1; bool paragraph=false; int style=0,fi=1,height=21; };
struct Page {size_t first,count;uint32_t anchor;};
struct Block {uint32_t start;int style;int direction=-1;};
static std::vector<Page> pages;
static std::vector<Block> blocks;
static std::unordered_map<std::string,uint32_t> htmlAnchors;
enum Screen {Dashboard,Library,Reading,ReadMenu,Chapters,Marks,Settings,Notice,Gallery,Picture,PictureMenu,Transfer,Clock,FontLab};
static ReaderFontLab fontLab;
static bool grayProbeActive=false;
static uint32_t grayProbeSince=0,grayProbeMs=0;
static Screen fontLabReturn=Dashboard;
static std::string fontLabSession;
static void enterFontLab();
static Screen screen=Dashboard,returnScreen=Dashboard,marksReturn=Dashboard,noticeReturn=Dashboard,clockReturn=Dashboard;
static ReaderClock readerClock;
static void enterClock(bool synchronize=true);
static int dashboardRow=0,menuRow=0,chapterRow=0,marksRow=0;
static std::vector<int> tocItems;
static std::vector<NamedBookmark> savedMarks;
static std::vector<BookMetadata> metadata;
static uint32_t lastInteraction=0,lastSleepAttempt=0;
static bool sleepRequested=false;
static int sleepTimer=0;static uint8_t sleepInjected=255;
static std::string toast;
static std::vector<std::string> gallery;
static int galleryRow=0,pictureMenuRow=0;
static ReaderTransfer transfer;
static std::string transferSession;
static bool transferWebsiteQR=false;
static uint32_t transferRenderedAt=0,transferRenderedPosition=0;
static bool transferRenderedConnection=false,transferRenderedBusy=false;
static void enterTransfer();
static void leaveTransfer();
static const char *screenName(){const char *names[]={"dashboard","library","reading","menu","chapters","bookmarks","settings","notice","gallery","picture","picture_menu","transfer","clock","font_comparison"};return names[screen];}

static std::vector<std::string> images;
static std::string chapterPath;
static bool panelReady=false,displayHealthy=true;
static unsigned partialCount=0;
static std::vector<Line> lines;
static bidi_char visual[BIDI_MAX_LINE];
static int pageCount() {return std::max(1,int(pages.size()));}

static uint32_t nextCp(const std::string &s, size_t &i) {
  return readerNextCodepoint(s,i);
}
static void appendCp(std::string &s, uint32_t cp) {
  if (cp < 128) s += char(cp);
  else if (cp < 2048) { s += char(0xC0|(cp>>6)); s += char(0x80|(cp&63)); }
  else if (cp < 65536) { s += char(0xE0|(cp>>12)); s += char(0x80|((cp>>6)&63)); s += char(0x80|(cp&63)); }
  else if (cp <= 0x10FFFF) { s += char(0xF0|(cp>>18)); s += char(0x80|((cp>>12)&63)); s += char(0x80|((cp>>6)&63)); s += char(0x80|(cp&63)); }
}
static const Glyph &glyph(uint32_t cp, int fi, bool body=false) {
  const Glyph *tables[]={glyphs14,glyphs16,glyphs18,glyphs20,glyphs12};
  const Glyph *g=body?(fi<4&&storage.settings.fontFamily==3?screenGlyphs[fi]:fi<4&&storage.settings.fontFamily?familyGlyphs[storage.settings.fontFamily-1][fi]:tables[fi]):uiGlyphs[fi];
  int lo=0,hi=glyphCount;
  while(lo<hi) { int m=(lo+hi)/2; if(g[m].cp<cp)lo=m+1;else hi=m; }
  if(lo<glyphCount && g[lo].cp==cp)return g[lo];
  return g[glyphCount-1];
}
static bool hiddenCp(uint32_t cp) { return (cp>=0x200B && cp<=0x200F) || (cp>=0x202A && cp<=0x202E) || (cp>=0x2066 && cp<=0x2069); }
static int advance(uint32_t cp, int fi, bool body=false) { return hiddenCp(cp) ? 0 : glyph(cp,fi,body).advance; }
static bool paragraphRtl(const std::string &s, size_t start) {
  while(start<s.size() && s[start]!='\n') { uint32_t cp=nextCp(s,start); uchar cls=bidi_class(cp); if(cls==R || cls==AL)return true;if(cls==L)return false; }
  return false;
}
static void drawGlyph(uint32_t cp, int x, int baseline, int fi, bool body=false) {
  if(hiddenCp(cp)) return;
  const Glyph &g=glyph(cp,fi,body);
  const uint8_t *tables[]={fontBits14,fontBits16,fontBits18,fontBits20,fontBits12};
  const uint8_t *bits=body?(fi<4&&storage.settings.fontFamily==3?screenBits[fi]:fi<4&&storage.settings.fontFamily?familyBits[storage.settings.fontFamily-1][fi]:tables[fi]):uiBits[fi];
  for(int y=0;y<g.h;y++)for(int xx=0;xx<g.w;xx++) {
    int bit=y*g.w+xx;
    if(bits[g.offset+bit/8]&(0x80>>(bit%8))) {
      int px=x+g.x+xx,py=baseline+g.y+y;
      if(px>=0&&px<viewW()&&py>=0&&py<viewH())Paint_SetPixel(px,py,BLACK);
    }
  }
}
static void drawText(const std::string &s, int left, int baseline, int fi, bool rtl=false, int width=376, bool body=false) {
  size_t pos=0; int count=0,total=0;
  while(pos<s.size() && count<BIDI_MAX_LINE) {
    uint32_t cp=nextCp(s,pos); visual[count]={cp,cp,uint16_t(count),0};total+=advance(cp,fi,body);++count;
  }
  do_bidi(false,rtl?1:0,visual,count);
  int x=rtl?left+width-total:left,lastBaseX=x;
  auto emit=[&](uint32_t cp) { if(bidi_class(cp)==NSM) drawGlyph(cp,lastBaseX,baseline,fi,body); else {lastBaseX=x;drawGlyph(cp,x,baseline,fi,body);x+=advance(cp,fi,body);} };
  for(int i=0;i<count;i++) {
    if(bidi_class(visual[i].wc)==NSM) {
      int j=i;while(j<count&&bidi_class(visual[j].wc)==NSM)++j;
      if(j<count&&visual[j].index<visual[i].index) { emit(visual[j].wc);for(int k=j-1;k>=i;k--)emit(visual[k].wc);i=j;continue; }
    }
    emit(visual[i].wc);
  }
}
static std::string clipped(const std::string &s,int maxWidth,int fi) {
  size_t i=0,good=0;int width=0;
  while(i<s.size()) {uint32_t cp=nextCp(s,i);width+=advance(cp,fi);if(width>maxWidth)break;good=i;}
  return s.substr(0,good);
}
static void rotateCanvas() {
  memset(frame,0xFF,sizeof(frame));int stride=(viewW()+7)/8;
  for(int y=0;y<viewH();y++)for(int x=0;x<viewW();x++){
    if(canvas[y*stride+x/8]&(0x80>>(x&7)))continue;
    int px=x,py=y;
    switch(storage.settings.rotation){
      case 90:px=399-y;py=x;break;
      case 180:px=399-x;py=299-y;break;
      case 270:px=y;py=299-x;break;
    }
    frame[py*50+px/8]&=~(0x80>>(px&7));
  }
}
static void message(const std::string &s) {
  Paint_NewImage(canvas,viewW(),viewH(),0,WHITE);EPD_Full(WHITE);
  drawText("CROWPANEL READER",12,30,1);
  drawText(clipped(s,376,1),12,100,1,paragraphRtl(s,0));
  rotateCanvas();EPD_Init();EPD_Display(frame);EPD_Sleep();
}
static void scanInputs(void *) {
  bool raw[5],stable[5];uint32_t changed[5]={};
  for(int i=0;i<5;i++)raw[i]=stable[i]=digitalRead(buttonPins[i])==LOW;
  while(true) {
    uint32_t now=millis();
    for(uint8_t i=0;i<5;i++) {
      bool pressed=digitalRead(buttonPins[i])==LOW;
      if(pressed!=raw[i]){raw[i]=pressed;changed[i]=now;}
      if(pressed!=stable[i]&&uint32_t(now-changed[i])>=25){stable[i]=pressed;if(pressed&&xQueueSend(inputQueue,&i,0)!=pdTRUE)Serial.println("QUEUE_FULL");}
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
static void findBooks(File &dir,int depth) {
  if(depth>5||books.size()>=128)return;
  File f;
  while((f=dir.openNextFile())) {
    std::string path=f.path(),name=f.name();
    if(!name.empty()&&name[0]!='.') {
      if(f.isDirectory())findBooks(f,depth+1);
      else if(path.size()>4) {
        size_t dot=path.find_last_of('.');std::string ext=dot==std::string::npos?"":path.substr(dot);for(char &c:ext)c=tolower((unsigned char)c);
        if((ext==".epub"||ext==".txt")&&books.size()<128)books.push_back(path);
      }
    }
    f.close();
  }
}
static void libraryScan() {
  bool hadBooks=!books.empty(),wasSample=selected==int(books.size());
  std::string selectedPath=selected>=0&&selected<int(books.size())?books[selected]:"";
  books.clear();books.reserve(128);
  if(sdReady){File root=SD.open("/");if(root)findBooks(root,0);root.close();}
  std::sort(books.begin(),books.end());
  selected=std::min(selected,int(books.size()));
  if(hadBooks&&wasSample)selected=books.size();
  for(size_t i=0;i<books.size();++i)if(books[i]==selectedPath){selected=i;break;}
  metadata.clear();metadata.resize(books.size());
  std::string previousImage=gallery.empty()?"":gallery[std::min(galleryRow,int(gallery.size())-1)];gallery=scanGallery(sdReady);galleryRow=0;
  for(size_t index=0;index<gallery.size();++index)if(gallery[index]==previousImage)galleryRow=index;
  Serial.printf("LIBRARY books=%u\n",unsigned(books.size()));
  for(auto &p:books)Serial.printf("BOOK %s\n",p.c_str());
}
static uint32_t currentOffset(){return pages.empty()?0:pages[std::min(size_t(page),pages.size()-1)].anchor;}
static void restoreOffset(uint32_t offset){page=0;for(size_t i=0;i<pages.size();i++)if(pages[i].anchor==offset){page=i;return;}}
static void savePosition() {
  if(!inBook||demo||!book||lines.empty())return;
  storage.saveBookmark(book->get_path(),Bookmark{1,uint32_t(chapter),currentOffset()},forcedBoundary);
}
static uint32_t entity(const std::string &s) {
  if(s=="amp")return '&';if(s=="lt")return '<';if(s=="gt")return '>';if(s=="quot")return '"';if(s=="apos")return '\'';
  if(s=="nbsp")return ' ';if(s=="mdash")return 0x2014;if(s=="ndash")return 0x2013;if(s=="hellip")return 0x2026;
  if(s=="lsquo")return 0x2018;if(s=="rsquo")return 0x2019;if(s=="ldquo")return 0x201C;if(s=="rdquo")return 0x201D;
  if(!s.empty()&&s[0]=='#'){char *end=nullptr;unsigned long cp=strtoul(s.c_str()+(s.size()>1&&(s[1]=='x'||s[1]=='X')?2:1),&end,s.size()>1&&(s[1]=='x'||s[1]=='X')?16:10);if(end&&!*end&&cp>0&&cp<=0x10FFFF)return cp;}
  return 0;
}
static std::string htmlText(const char *html,size_t size) {
  blocks.clear();htmlAnchors.clear();std::string out;out.reserve(size);std::string skip;
  struct Semantic {std::string name;int style,direction;};std::vector<Semantic> semantics;
  auto active=[&](){Semantic result{"",0,-1};for(auto &value:semantics){if(value.style)result.style=value.style;if(value.direction>=0)result.direction=value.direction;}return result;};
  auto newline=[&](){while(!out.empty()&&out.back()==' ')out.pop_back();if(!out.empty()&&out.back()!='\n')out+='\n';};
  for(size_t i=0;i<size;) {
    char c=html[i];
    if(c=='<') {
      if(i+4<size&&std::string(html+i,4)=="<!--"){const char *end=strstr(html+i+4,"-->");i=end?size_t(end-html)+3:size;continue;}
      size_t end=i+1;char quote=0;
      for(;end<size;end++){char t=html[end];if(quote){if(t==quote)quote=0;}else if(t=='\''||t=='"')quote=t;else if(t=='>')break;}
      std::string rawTag(html+i+1,end-i-1);
      std::string tag=rawTag;for(char &t:tag)t=tolower((unsigned char)t);
      bool closing=!tag.empty()&&tag[0]=='/';size_t start=closing?1:0,finish=start;
      while(finish<tag.size()&&(isalnum((unsigned char)tag[finish])||tag[finish]==':'))finish++;
      std::string name=tag.substr(start,finish-start);size_t colon=name.find(':');if(colon!=std::string::npos)name=name.substr(colon+1);
      if(!skip.empty()){if(closing&&name==skip)skip.clear();}
      else if(!closing&&(name=="head"||name=="style"||name=="script"))skip=name;
      else if(name=="p"||name=="div"||name=="br"||name=="li"||name=="h1"||name=="h2"||name=="h3"||name=="h4"||name=="blockquote"||name=="tr"||name=="section"||name=="hr")newline();
      else if((name=="img"||name=="image")&&!closing){
        auto attr=[&](const std::string &key)->std::string {
          size_t at=tag.find(key+"=");if(at==std::string::npos)at=tag.find(key+" =");
          if(at==std::string::npos)return "";at=rawTag.find('=',at)+1;
          while(at<rawTag.size()&&isspace((unsigned char)rawTag[at]))++at;
          if(at>=rawTag.size())return "";char q=rawTag[at];
          if(q=='\''||q=='"'){size_t e=rawTag.find(q,at+1);return rawTag.substr(at+1,e-at-1);}
          size_t e=rawTag.find_first_of(" />",at);return rawTag.substr(at,e-at);
        };
        std::string href=attr(name=="img"?"src":"xlink:href");if(href.empty())href=attr("href");
        if(!href.empty()){newline();out+="[[IMG:"+href+"]]";newline();}
      }
      if(skip.empty()){
        if(!closing){
          for(const char *key:{"id","name"}){
            size_t at=tag.find(std::string(key)+"=");if(at==std::string::npos)at=tag.find(std::string(key)+" =");
            if(at!=std::string::npos){at=rawTag.find('=',at)+1;while(at<rawTag.size()&&isspace((unsigned char)rawTag[at]))++at;
              if(at<rawTag.size()&&(rawTag[at]=='\''||rawTag[at]=='"')){char q=rawTag[at++];size_t e=rawTag.find(q,at);if(e!=std::string::npos)htmlAnchors[rawTag.substr(at,e-at)]=out.size();}}
          }
        }
        if(closing){for(size_t j=semantics.size();j>0;j--)if(semantics[j-1].name==name){semantics.resize(j-1);break;}}
        else if(name!="br"&&name!="img"&&name!="image"&&name!="hr"&&name!="meta"&&name!="link"&&semantics.size()<64){
          int kind=(name=="h1"||name=="h2"||name=="h3"||name=="h4")?1:name=="li"?2:name=="blockquote"?3:0,direction=-1;
          size_t at=tag.find("dir=");if(at==std::string::npos)at=tag.find("dir =");
          if(at!=std::string::npos){size_t eq=tag.find('=',at);std::string value=tag.substr(eq+1);if(value.find("rtl")<8)direction=1;else if(value.find("ltr")<8)direction=0;}
          semantics.push_back({name,kind,direction});
        }
        auto value=active();blocks.push_back({uint32_t(out.size()),value.style,value.direction});
      }
      i=std::min(end+1,size);continue;
    }
    if(!skip.empty()){++i;continue;}
    if(c=='&') {
      size_t end=i+1;while(end<size&&end-i<24&&html[end]!=';'&&html[end]!=' '&&html[end]!='<')end++;
      if(end<size&&html[end]==';'){uint32_t cp=entity(std::string(html+i+1,end-i-1));if(cp){appendCp(out,cp);i=end+1;continue;}}
    }
    if(isspace((unsigned char)c)){if(!out.empty()&&out.back()!=' '&&out.back()!='\n')out+=' ';}
    else out+=c;
    ++i;
  }
  while(!out.empty()&&isspace((unsigned char)out.back()))out.pop_back();
  return out;
}
// A forced boundary participates in both preceding and following pagination.
// UTF-8 offsets remain in the original canonical chapter text, including image markers.
static void paginate(uint32_t anchor=UINT32_MAX) {
  images.clear();lines.clear();pages.clear();lines.reserve(chapterText.size()/30+8);forcedBoundary=anchor;
  if(anchor!=UINT32_MAX){anchor=std::min(anchor,uint32_t(chapterText.size()));while(anchor>0&&anchor<chapterText.size()&&(uint8_t(chapterText[anchor])&0xC0)==0x80)--anchor;}
  size_t pos=0;bool rtl=false,newParagraph=true;int style=0,direction=-1;size_t block=0;
  while(pos<chapterText.size()) {
    while(pos<chapterText.size()&&chapterText[pos]==' '&&pos!=anchor)++pos;
    if(pos>=chapterText.size())break;
    while(block<blocks.size()&&blocks[block].start<=pos){style=blocks[block].style;direction=blocks[block].direction;++block;}
    if(book&&!book->is_text()&&chapterText.compare(pos,6,"[[IMG:")==0){
      size_t end=chapterText.find("]]",pos+6);if(end!=std::string::npos){
        std::string href=chapterText.substr(pos+6,end-pos-6),base=chapterPath.substr(0,chapterPath.find_last_of('/')+1);size_t fragment=href.find('#');if(fragment!=std::string::npos)href.resize(fragment);
        images.push_back(!href.empty()&&href[0]=='/'?href.substr(1):base+href);
        lines.push_back({uint32_t(pos),uint32_t(end+2),false,int(images.size()-1),false,0,fontIndex,viewH()});
        pos=end+2;while(pos<chapterText.size()&&chapterText[pos]=='\n')++pos;newParagraph=true;continue;
      }
    }
    if(chapterText[pos]=='\n'){++pos;newParagraph=true;continue;}
    bool paragraph=newParagraph;if(newParagraph){rtl=direction>=0?direction==1:paragraphRtl(chapterText,pos);newParagraph=false;}
    int fi=style==1?std::min(3,fontIndex+1):fontIndex;
    int indent=(style==2||style==3)?16:paragraph&&style==0?12:0;
    size_t start=pos,end=pos,lastSpace=std::string::npos;int width=0,count=0;
    bool forced=false;
    while(pos<chapterText.size()&&chapterText[pos]!='\n'&&count<BIDI_MAX_LINE-4){
      if(pos==anchor&&pos>start){forced=true;break;}
      size_t before=pos;uint32_t cp=nextCp(chapterText,pos);int w=advance(cp,fi,true);
      if(width+w>viewW()-2*storage.settings.margin-indent&&before>start){pos=before;break;}
      width+=w;end=pos;++count;if(cp==' ')lastSpace=before;
    }
    if(!forced&&pos<chapterText.size()&&chapterText[pos]!='\n'&&lastSpace!=std::string::npos&&lastSpace>start){end=lastSpace;pos=lastSpace+1;}
    while(end>start&&chapterText[end-1]==' ')--end;
    lines.push_back({uint32_t(start),uint32_t(end),rtl,-1,paragraph,style,fi,fontSizes[fi]+storage.settings.lineGap+(paragraph?4:0)});
    if(pos<chapterText.size()&&chapterText[pos]=='\n'){++pos;newParagraph=true;}
    if(pos==start)++pos;
  }
  if(lines.empty())lines.push_back({0,0,false,-1,true,0,fontIndex,fontSizes[fontIndex]+storage.settings.lineGap});
  int available=viewH()-(storage.settings.showTitle?30:14)-(storage.settings.showProgress?26:12),used=0;
  size_t first=0;
  auto finish=[&](size_t end){if(end>first){pages.push_back({first,end-first,lines[first].start});first=end;used=0;}};
  for(size_t i=0;i<lines.size();i++){
    auto &l=lines[i];if(l.start==anchor)finish(i);
    if(l.image>=0){finish(i);pages.push_back({i,1,l.start});first=i+1;used=0;continue;}
    int h=l.height;if(i==first&&l.paragraph)h-=4;
    if(used+h>available&&i>first){finish(i);h=l.height-(l.paragraph?4:0);}
    used+=h;
  }
  finish(lines.size());page=std::min(page,pageCount()-1);if(anchor!=UINT32_MAX)restoreOffset(anchor);
}
static bool loadChapter(int index) {
  if(!book||index<0||index>=book->get_spine_items_count())return false;
  size_t size=0;uint8_t *data=book->get_item_contents(book->get_spine_item(index),&size);
  if(!data){errorText="Cannot read chapter (max 1 MB).";Serial.printf("CHAPTER_ERROR index=%d\n",index);return false;}
  chapterPath=book->get_spine_item(index);
  if(book->is_text()){
    blocks.clear();htmlAnchors.clear();images.clear();chapterText.clear();
    auto append=[&](uint32_t cp){if(cp<128)chapterText+=char(cp);else if(cp<2048){chapterText+=char(192|(cp>>6));chapterText+=char(128|(cp&63));}else if(cp<65536){chapterText+=char(224|(cp>>12));chapterText+=char(128|((cp>>6)&63));chapterText+=char(128|(cp&63));}else{chapterText+=char(240|(cp>>18));chapterText+=char(128|((cp>>12)&63));chapterText+=char(128|((cp>>6)&63));chapterText+=char(128|(cp&63));}};
    if(size>=2&&((data[0]==255&&data[1]==254)||(data[0]==254&&data[1]==255))){
      bool le=data[0]==255;auto unit=[&](size_t i){return uint32_t(le?data[i]|(data[i+1]<<8):(data[i]<<8)|data[i+1]);};
      for(size_t i=2;i+1<size;i+=2){uint32_t cp=unit(i);if(cp>=0xD800&&cp<=0xDBFF&&i+3<size){uint32_t low=unit(i+2);if(low>=0xDC00&&low<=0xDFFF){cp=0x10000+((cp-0xD800)<<10)+low-0xDC00;i+=2;}else cp=0xFFFD;}else if(cp>=0xD800&&cp<=0xDFFF)cp=0xFFFD;append(cp);}
    }else {size_t start=size>=3&&data[0]==239&&data[1]==187&&data[2]==191?3:0;chapterText.assign((char*)data+start,size-start);}
    std::string normal;normal.reserve(chapterText.size());for(size_t i=0;i<chapterText.size();++i){char c=chapterText[i];if(c=='\r'){normal+='\n';if(i+1<chapterText.size()&&chapterText[i+1]=='\n')++i;}else if(c=='\t')normal+="    ";else if(c)normal+=c;}chapterText.swap(normal);
    Serial.printf("TXT canonical_bytes=%u crc=%08lx\n",unsigned(chapterText.size()),(unsigned long)readerCRC((const uint8_t*)chapterText.data(),chapterText.size()));
  }else chapterText=htmlText((char *)data,size);free(data);
  if(chapterText.empty())chapterText="[This section has no readable text.]";
  chapter=index;page=0;paginate();
  Serial.printf("CHAPTER index=%d bytes=%u lines=%u pages=%d heap=%u psram_free=%u\n",chapter,unsigned(chapterText.size()),unsigned(lines.size()),pageCount(),ESP.getFreeHeap(),ESP.getFreePsram());
  return true;
}
static void loadingFrame();
static void openSelected() {
  inBook=false;errorText.clear();demo=selected==int(books.size());
  if(demo) {
    book.reset();chapter=page=0;
    chapterText="Hebrew and English sample\nשלום עולם! זהו מבחן קריאה בעברית.\nהמסך מציג טקסט מימין לשמאל, כולל מספרים 123 ומילים English בתוך המשפט.\nבדיקת סימני פיסוק (סוגריים), ושילוב שפות.\nשָׁלוֹם — גם ניקוד דורש בדיקה חזותית.\nEnglish reads left to right. The year is 2026.\nMenu opens settings. Up and Down turn pages. Exit returns to the library.";
    blocks.clear();htmlAnchors.clear();paginate();inBook=true;screen=Reading;return;
  }
  File source=SD.open(books[selected].c_str(),FILE_READ);size_t sourceSize=source?source.size():0;source.close();
  if(panelReady&&sourceSize>128*1024)loadingFrame();
  book.reset(new(std::nothrow) Epub("/sd"+books[selected]));
  if(!book||!book->load()){
    std::string name=books[selected];for(char &c:name)c=tolower((unsigned char)c);
    bool text=name.size()>=4&&name.substr(name.size()-4)==".txt";book.reset();
    errorText=text?"This TXT cannot be opened. Use UTF-8 or UTF-16 text under 1 MiB.":"This EPUB cannot be opened. It may be damaged or encrypted. Try an unencrypted EPUB file.";return;
  }
  Serial.printf("OPEN title=%s chapters=%d\n",book->get_title().c_str(),book->get_spine_items_count());
  Bookmark saved{};bool hasSaved=storage.loadBookmark(book->get_path(),saved);
  int start=hasSaved&&saved.chapter<uint32_t(book->get_spine_items_count())?saved.chapter:0;
  if(!loadChapter(start)){book.reset();return;}
  if(hasSaved&&start==int(saved.chapter)){
    uint32_t boundary=storage.loadBoundary(book->get_path(),saved.offset);paginate(boundary);restoreOffset(saved.offset);if(currentOffset()!=saved.offset)paginate(saved.offset);
  }
  Serial.printf("RESUME found=%d chapter=%d offset=%u page=%d\n",hasSaved,chapter,hasSaved?saved.offset:0,page);
  inBook=true;screen=Reading;
}
static std::string bookTitle(){return demo?"A little reading sample":book?book->get_title():"";}
static void label(const std::string &text,int x,int y,int fi,int width){std::string value=clipped(text,width,fi);drawText(value,x,y,fi,paragraphRtl(value,0),width);}
static void box(int x,int y,int w,int h,bool chosen=false){EPD_DrawRectangle(x,y,x+w-1,y+h-1,BLACK,0);if(chosen)EPD_DrawRectangle(x+2,y+2,x+w-3,y+h-3,BLACK,0);}
static void rule(int y){EPD_DrawLine(18,y,viewW()-19,y,BLACK);}
static void header(const std::string &title,const std::string &subtitle=""){
  label(title,20,32,3,viewW()-40);rule(44);if(!subtitle.empty())label(subtitle,20,64,4,viewW()-40);
}
static void footer(const std::string &text){rule(viewH()-33);label(text,20,viewH()-12,4,viewW()-40);}
static void wrapped(const std::string &text,int x,int y,int fi,int width,int count,int pitch){
  size_t pos=0;for(int i=0;i<count&&pos<text.size();i++){
    std::string part=clipped(text.substr(pos),width,fi);if(part.empty())break;
    if(pos+part.size()<text.size()){size_t space=part.find_last_of(' ');if(space!=std::string::npos&&space>0)part.resize(space);}
    label(part,x,y+i*pitch,fi,width);pos+=part.size();while(pos<text.size()&&(text[pos]==' '||text[pos]=='\n'))++pos;
  }
}
static void loadingFrame(){
  havePresented=false;
  Paint_NewImage(canvas,viewW(),viewH(),0,WHITE);EPD_Full(WHITE);header("Opening your book");
  label("Returning to your saved passage",20,112,1,viewW()-40);label("One moment…",20,145,4,viewW()-40);
  for(int i=0;i<3;i++)EPD_DrawRectangle(20+i*18,174,27+i*18,181,BLACK,1);rotateCanvas();
  bool full=storage.settings.fullRefreshEvery==0||partialCount>=unsigned(storage.settings.fullRefreshEvery);EPD_ClearError();
  if(full){EPD_Init();EPD_Display(frame);partialCount=0;}else{EPD_Display_Part(0,0,400,300,frame);++partialCount;}
  EPD_Address_Set(0,0,399,299);EPD_SetCursor(0,0);EPD_WR_REG(0x26);for(size_t i=0;i<sizeof(frame);i++)EPD_WR_DATA8(frame[i]);panelReady=!EPD_HasError();
}
static BookMetadata &info(int index){
  BookMetadata &value=metadata[index];if(!value.valid)value=cachedMetadata(books[index],sdReady);return value;
}
static void cover(int index,int x,int y,int w,int h){
  bool ok=false;if(index>=0&&index<int(books.size())){
    auto &meta=info(index);if(!meta.cover.empty()){Epub epub("/sd"+books[index]);ok=cachedImage(epub,meta.cover,canvas,viewW(),viewH(),x,y,w,h,sdReady);}
  }
  if(!ok){box(x,y,w,h);int fi=w<65?4:1;std::string ext=index>=0&&index<int(books.size())?books[index]:"";for(char &c:ext)c=tolower((unsigned char)c);label(ext.size()>=4&&ext.substr(ext.size()-4)==".txt"?"TXT":"EPUB",x+8,y+h/2,fi,w-16);EPD_DrawLine(x+8,y+12,x+w-9,y+12,BLACK);}
}
static int bookIndex(const std::string &path){for(size_t i=0;i<books.size();i++)if("/sd"+books[i]==path||books[i]==path)return i;return -1;}
static std::string progressLabel(int index){
  if(index<0||index>=int(books.size()))return "Ready to read";
  Bookmark saved{};auto &meta=info(index);if(!storage.loadBookmark("/sd"+books[index],saved))return "Not started";
  int percent=meta.chapters>0?std::min(99,int(saved.chapter*100/meta.chapters)):0;
  return std::to_string(percent)+"%  ·  Section "+std::to_string(saved.chapter+1);
}
static void notice(const std::string &text,Screen previous){errorText=text;noticeReturn=previous;screen=Notice;inSettings=false;}
static void listRows(const std::vector<std::string> &labels,int chosen,int top=98,int pitch=38){
  int visible=std::max(1,(viewH()-top-43)/pitch),start=(chosen/visible)*visible;
  for(int i=start;i<int(labels.size())&&i<start+visible;i++){
    int y=top+(i-start)*pitch;box(18,y-21,viewW()-36,pitch-3,i==chosen);label(labels[i],30,y+2,1,viewW()-60);
  }
  if(labels.size()>size_t(visible))label(std::to_string(chosen+1)+" / "+std::to_string(labels.size()),viewW()-82,viewH()-40,4,64);
}
static std::string chapterName(){
  if(!book)return "Sample";std::string result="Section "+std::to_string(chapter+1);
  for(int i=0;i<book->get_toc_items_count();i++)if(book->get_spine_index_for_toc_index(i)==chapter){result=book->get_toc_item(i).title;break;}return result;
}
static void refreshToc(){
  tocItems.clear();if(book)for(int i=0;i<book->get_toc_items_count();i++)if(book->get_spine_index_for_toc_index(i)>=0)tocItems.push_back(i);
  chapterRow=0;for(size_t i=0;i<tocItems.size();i++)if(book->get_spine_index_for_toc_index(tocItems[i])==chapter){chapterRow=i;break;}
}
static void refreshMarks(bool forBook){savedMarks=storage.namedBookmarks(forBook&&book?book->get_path():"");marksRow=0;}
static std::string sessionText(){
  if(screen==FontLab&&!fontLabSession.empty())return fontLabSession;
  cJSON *doc=cJSON_CreateObject();cJSON_AddNumberToObject(doc,"version",3);cJSON_AddNumberToObject(doc,"screen",screen);
  cJSON_AddStringToObject(doc,"book",book&&!demo?book->get_path().c_str():"");cJSON_AddBoolToObject(doc,"demo",demo&&inBook);
  cJSON_AddNumberToObject(doc,"chapter",chapter);cJSON_AddNumberToObject(doc,"offset",currentOffset());cJSON_AddNumberToObject(doc,"boundary",forcedBoundary==UINT32_MAX?-1:double(forcedBoundary));
  cJSON_AddNumberToObject(doc,"selected",selected);cJSON_AddNumberToObject(doc,"dashboard_row",dashboardRow);
  cJSON_AddStringToObject(doc,"selected_path",selected<int(books.size())?books[selected].c_str():"");
  cJSON_AddNumberToObject(doc,"menu_row",menuRow);cJSON_AddNumberToObject(doc,"settings_row",settingsRow);
  cJSON_AddNumberToObject(doc,"chapter_row",chapterRow);cJSON_AddNumberToObject(doc,"marks_row",marksRow);
  cJSON_AddStringToObject(doc,"image",gallery.empty()?"":gallery[galleryRow].c_str());cJSON_AddNumberToObject(doc,"gallery_row",galleryRow);cJSON_AddNumberToObject(doc,"picture_menu_row",pictureMenuRow);
  cJSON_AddNumberToObject(doc,"return_screen",returnScreen);cJSON_AddNumberToObject(doc,"marks_return",marksReturn);
  cJSON_AddNumberToObject(doc,"settings_anchor",settingsAnchor);
  cJSON_AddNumberToObject(doc,"clock_return",clockReturn);
  cJSON_AddStringToObject(doc,"notice",errorText.c_str());cJSON_AddNumberToObject(doc,"notice_return",noticeReturn);
  cJSON_AddNumberToObject(doc,"font",storage.settings.fontPx);cJSON_AddNumberToObject(doc,"rotation",storage.settings.rotation);
  cJSON_AddNumberToObject(doc,"font_profile",fontProfile);
  cJSON_AddNumberToObject(doc,"font_family",storage.settings.fontFamily);
  cJSON_AddBoolToObject(doc,"text_smoothing",storage.settings.smoothText);
  cJSON_AddNumberToObject(doc,"settings_layout",5);
  cJSON_AddBoolToObject(doc,"advanced_settings",advancedSettings);
  cJSON_AddNumberToObject(doc,"margin",storage.settings.margin);cJSON_AddNumberToObject(doc,"gap",storage.settings.lineGap);
  char *text=cJSON_PrintUnformatted(doc);std::string result=text?text:"";cJSON_free(text);cJSON_Delete(doc);return result;
}
static int jsonInt(cJSON *doc,const char *key,int fallback,int maximum=2147483647){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(doc,key);return cJSON_IsNumber(v)&&v->valuedouble>=0&&v->valuedouble<=maximum&&v->valuedouble==int(v->valuedouble)?v->valueint:fallback;
}
static bool restoreSession(const std::string &text){
  sessionFrameCompatible=false;
  cJSON *doc=cJSON_Parse(text.c_str());int version=jsonInt(doc,"version",0);if(!cJSON_IsObject(doc)||(version!=2&&version!=3)){cJSON_Delete(doc);return false;}
  Screen target=Screen(jsonInt(doc,"screen",Dashboard,Clock));
  Screen savedClockReturn=Screen(jsonInt(doc,"clock_return",Dashboard,PictureMenu));
#if !READER_ENABLE_CLOCK_SCREENSAVER
  if(target==Clock)target=savedClockReturn;
#endif
  Screen bookTarget=target==Clock?savedClockReturn:target;
  cJSON *path=cJSON_GetObjectItemCaseSensitive(doc,"book"),*sample=cJSON_GetObjectItemCaseSensitive(doc,"demo");
  int index=cJSON_IsString(path)?bookIndex(path->valuestring):-1;
  bool needsBook=bookTarget==Reading||bookTarget==ReadMenu||bookTarget==Chapters||(bookTarget==Settings&&jsonInt(doc,"return_screen",Dashboard,Notice)==Reading);
  if(index>=0||cJSON_IsTrue(sample)){
    selected=index>=0?index:books.size();openSelected();
    if(inBook){int targetChapter=demo?0:jsonInt(doc,"chapter",0,book->get_spine_items_count()-1);if(demo||targetChapter==chapter||loadChapter(targetChapter)){
      cJSON *pivot=cJSON_GetObjectItemCaseSensitive(doc,"boundary");uint32_t boundary=cJSON_IsNumber(pivot)&&pivot->valuedouble==-1?UINT32_MAX:jsonInt(doc,"boundary",jsonInt(doc,"offset",0));
      uint32_t offset=jsonInt(doc,"offset",0);paginate(boundary);restoreOffset(offset);if(currentOffset()!=offset)paginate(offset);
    }}
  }
  bool valid=!needsBook||inBook;
  if(valid){
    screen=target;clockReturn=savedClockReturn;selected=jsonInt(doc,"selected",selected,books.size());cJSON *selectedPath=cJSON_GetObjectItemCaseSensitive(doc,"selected_path");if(cJSON_IsString(selectedPath)){if(!selectedPath->valuestring[0])selected=books.size();else{int selectedIndex=bookIndex(selectedPath->valuestring);if(selectedIndex>=0)selected=selectedIndex;}}dashboardRow=jsonInt(doc,"dashboard_row",0,4);if(version==2&&dashboardRow==3)dashboardRow=4;
    menuRow=jsonInt(doc,"menu_row",0,readerMenuCount()-1);settingsRow=jsonInt(doc,"settings_row",0,11);returnScreen=Screen(jsonInt(doc,"return_screen",Dashboard,PictureMenu));
    advancedSettings=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc,"advanced_settings"));
    if(jsonInt(doc,"settings_layout",1)<3){
      const int oldIds[]={0,11,1,2,3,4,5,6,7,8,9,10};
      int oldRow=settingsRow;if(jsonInt(doc,"settings_layout",1)==1&&oldRow>=1)++oldRow;
      int id=oldIds[std::min(oldRow,11)];advancedSettings=id==6||id==7||id==8;settingsRow=0;
      for(int row=0;row<readerSettingsCount(advancedSettings);++row)if(readerSettingId(advancedSettings,row)==id)settingsRow=row;
    }
    if(jsonInt(doc,"settings_layout",1)==4&&!advancedSettings&&settingsRow>=2){if(settingsRow>2)--settingsRow;else settingsRow=0;}
    settingsRow=std::min(settingsRow,readerSettingsCount(advancedSettings)-1);
    marksReturn=Screen(jsonInt(doc,"marks_return",Dashboard,PictureMenu));settingsAnchor=jsonInt(doc,"settings_anchor",currentOffset());
    galleryRow=jsonInt(doc,"gallery_row",0,std::max(0,int(gallery.size())-1));bool imageFound=false;cJSON *image=cJSON_GetObjectItemCaseSensitive(doc,"image");if(cJSON_IsString(image))for(size_t index=0;index<gallery.size();++index)if(gallery[index]==image->valuestring){galleryRow=index;imageFound=true;}
    pictureMenuRow=jsonInt(doc,"picture_menu_row",0,2);if((target==Picture||target==PictureMenu)&&(!imageFound||gallery.empty()))screen=Gallery;
    if(screen==Chapters){refreshToc();chapterRow=jsonInt(doc,"chapter_row",chapterRow,std::max(0,int(tocItems.size())-1));}
    if(screen==Marks){refreshMarks(marksReturn==ReadMenu);marksRow=jsonInt(doc,"marks_row",0,std::max(0,int(savedMarks.size())-1));}
    inSettings=screen==Settings;noticeReturn=Screen(jsonInt(doc,"notice_return",Dashboard,Notice));cJSON *message=cJSON_GetObjectItemCaseSensitive(doc,"notice");if(cJSON_IsString(message))errorText=message->valuestring;
    if(needsBook&&jsonInt(doc,"offset",0)!=int(currentOffset()))valid=false;
    bool pixelsCompatible=valid;
    if(jsonInt(doc,"font",storage.settings.fontPx)!=storage.settings.fontPx||jsonInt(doc,"rotation",storage.settings.rotation)!=storage.settings.rotation||jsonInt(doc,"margin",storage.settings.margin)!=storage.settings.margin||jsonInt(doc,"gap",storage.settings.lineGap)!=storage.settings.lineGap)pixelsCompatible=false;
    if(jsonInt(doc,"font_family",0)!=storage.settings.fontFamily)pixelsCompatible=false;
    // A raster update invalidates the saved pixels, not the screen/book anchor.
    cJSON *savedSmooth=cJSON_GetObjectItemCaseSensitive(doc,"text_smoothing");
    sessionFrameCompatible=pixelsCompatible&&jsonInt(doc,"font_profile",0)==fontProfile&&cJSON_IsBool(savedSmooth)&&bool(cJSON_IsTrue(savedSmooth))==storage.settings.smoothText;
  }
  cJSON_Delete(doc);if(valid&&screen==Clock)readerClock.enter();return valid;
}
static void render(){
  int width=viewW(),height=viewH();Paint_NewImage(canvas,width,height,0,WHITE);EPD_Full(WHITE);
  if(screen==FontLab){
    if(!fontLab.draw(canvas,sizeof(canvas),width,height)){header("Comparison unavailable");footer("Exit: return");}
  }else if(screen==Clock){
    readerClock.draw(width,height,[](int x,int y){if(x>=0&&x<viewW()&&y>=0&&y<viewH())Paint_SetPixel(x,y,BLACK);});
  }else if(screen==Dashboard){
    header("Ink Reader",storage.settings.development?"OFFLINE  /  USB DEVELOPMENT":"OFFLINE  /  BATTERY MODE");
    int index=book&&!demo?bookIndex(book->get_path()):bookIndex(storage.lastBook());
    int top=78,cardH=height<width?104:170;box(18,top,width-36,cardH,dashboardRow==0);
    int cw=height<width?56:72,ch=cardH-22;cover(index,30,top+11,cw,ch);
    int tx=42+cw,tw=width-tx-30;label("CONTINUE READING",tx,top+25,4,tw);
    std::string title=index>=0?info(index).title:"Your next chapter awaits";
    wrapped(title,tx,top+51,1,tw,height<width?1:3,23);
    label(index>=0?info(index).author:"Choose a book from Library",tx,top+cardH-34,4,tw);
    label(progressLabel(index),tx,top+cardH-14,4,tw);
    int topCards=top+cardH+8,w=(width-42)/2,h=(height-41-topCards)/2;
    std::vector<std::string> labels={"Library · "+std::to_string(books.size()),"Bookmarks · "+std::to_string(storage.namedBookmarks().size()),"Images · "+std::to_string(gallery.size()),"Settings"};
    for(int index=0;index<4;index++){int x=18+(index%2)*(w+6),y=topCards+(index/2)*h;box(x,y,w,h-5,dashboardRow==index+1);label(labels[index],x+10,y+(h-5)/2+5,4,w-20);}
    footer("Up / Down: choose     Center: open");
  }else if(screen==Library){
    header("Library",sdReady?std::to_string(books.size())+" books  ·  On your microSD":"microSD is not available");
    if(books.empty()&&selected==0){wrapped(sdReady?"No books yet. Copy EPUB or TXT files to your microSD card, then rescan in Settings.":"Insert your microSD card and restart to open your library.",20,112,1,width-40,4,25);label("Center: try the reading sample",20,height-62,4,width-40);}
    else{
      int y=80,cw=width<height?86:90,ch=width<height?128:139;cover(selected,y==80?24:24,y,cw,ch);
      std::string title=selected<int(books.size())?info(selected).title:"English / Hebrew sample";
      std::string author=selected<int(books.size())?info(selected).author:"Built-in reading check";
      int tx=width<height?126:132,tw=width-tx-22;wrapped(title,tx,y+20,1,tw,4,24);wrapped(author,tx,y+122,4,tw,2,17);
      int by=width<height?238:224;box(18,by,width-36,35,true);label(progressLabel(selected),30,by+24,1,width-60);
      if(width<height){label("Selected book",20,306,4,width-40);label(std::to_string(selected+1)+" / "+std::to_string(books.size()+1)+"  ·  Center to read",20,334,1,width-40);}
    }
    footer("Up / Down: browse    Exit: dashboard");
  }else if(screen==Reading){
    int margin=storage.settings.margin,y=storage.settings.showTitle?30:14;
    bool fixed=book&&!demo&&book->is_fixed_layout();
    if(fixed){margin=0;y=0;}
    if(storage.settings.showTitle&&!fixed)label(bookTitle(),margin,17,4,width-2*margin);
    const Page &p=pages[std::min(size_t(page),pages.size()-1)];
    if(lines[p.first].image>=0&&!demo){
      if(!cachedImage(*book,images[lines[p.first].image],canvas,width,height,margin,y,width-2*margin,height-y-(fixed?0:storage.settings.showProgress?28:12),sdReady)){
        box(margin,y,width-2*margin,height-y-30);label("Image unavailable",margin+14,y+45,1,width-2*margin-28);wrapped("This image is unsupported or exceeds the available memory. Continue to the next page.",margin+14,y+76,4,width-2*margin-28,3,19);
      }
    }else for(size_t i=p.first;i<p.first+p.count;i++){
      const Line &l=lines[i];if(l.paragraph&&i!=p.first)y+=4;int indent=l.style==2||l.style==3?16:l.paragraph&&l.style==0?12:0;
      if(l.style==2&&l.paragraph)label("•",l.rtl?width-margin-8:margin,y+fontSizes[l.fi],l.fi,10);
      if(l.style==3)EPD_DrawLine(l.rtl?width-margin-4:margin+4,y,l.rtl?width-margin-4:margin+4,y+fontSizes[l.fi]+storage.settings.lineGap-1,BLACK);
      drawText(chapterText.substr(l.start,l.end-l.start),margin+(l.rtl?0:indent),y+fontSizes[l.fi],l.fi,l.rtl,width-2*margin-indent,true);y+=fontSizes[l.fi]+storage.settings.lineGap;
    }
    if(storage.settings.showProgress&&!fixed){
      label(std::to_string(page+1)+" / "+std::to_string(pageCount())+"   ·   "+std::to_string(fontSizes[fontIndex])+" px",margin,height-8,4,width-2*margin-110);
      double progress=demo?double(page+1)/pageCount():(chapter+double(page+1)/pageCount())/book->get_spine_items_count();int x=width-margin-90;
      EPD_DrawLine(x,height-13,width-margin,height-13,BLACK);EPD_DrawLine(x,height-14,x+int(90*progress),height-14,BLACK);
    }
  }else if(screen==ReadMenu){
    header("Reading menu",bookTitle());std::vector<std::string> labels={"Chapters","Add / remove bookmark","Saved bookmarks","Reading settings","Dashboard","Sleep"};if(READER_ENABLE_CLOCK_SCREENSAVER)labels.push_back("Screensaver");listRows(labels,menuRow);footer(toast.empty()?"Center: select    Exit: return to book":toast);
  }else if(screen==Settings){
    header(advancedSettings?"Advanced settings":returnScreen==Reading?"Reading settings":"Settings","Changes are saved automatically");
    const auto &cfg=storage.settings;
    std::vector<std::string> labels={"Text size: "+std::to_string(cfg.fontPx)+" px","Font: "+std::string(fontNames[cfg.fontFamily]),"Rotation: "+std::to_string(cfg.rotation)+"°","Margins: "+std::to_string(cfg.margin)+" px","Line spacing: "+std::to_string(cfg.lineGap)+" px","Book title: "+std::string(cfg.showTitle?"shown":"hidden"),"Progress: "+std::string(cfg.showProgress?"shown":"hidden"),"Clean refresh: "+std::string(cfg.fullRefreshEvery?std::to_string(cfg.fullRefreshEvery)+" turns":"every turn"),"Development mode: "+std::string(cfg.development?"on":"off"),"Rescan books and images","Sleep now","File Transfer"};
    labels.push_back("Advanced settings");labels.push_back("Back to settings");labels.push_back("Screensaver");labels.push_back("Font comparison");labels.push_back(std::string("Text smoothing: ")+(cfg.smoothText?"Smooth (slower)":"Fast"));
    std::vector<std::string> visible;
    for(int row=0;row<readerSettingsCount(advancedSettings);++row){int id=readerSettingId(advancedSettings,row);int labelIndex=id==0?0:id==11?1:id<11?id+1:id;visible.push_back(labels[labelIndex]);}
    listRows(visible,settingsRow,100,36);footer("Center: change     Exit: return");
  }else if(screen==Chapters){
    header("Chapters",bookTitle());std::vector<std::string> labels;
    if(book){if(tocItems.empty())for(int i=0;i<book->get_spine_items_count();i++)labels.push_back("Section "+std::to_string(i+1));
      else for(int index:tocItems){auto &entry=book->get_toc_item(index);labels.push_back(std::string(std::min(3,entry.level)*2,' ')+entry.title);}}
    if(labels.empty())label("No chapter list for the sample",20,112,1,width-40);else listRows(labels,chapterRow);
    footer("Center: go to chapter    Exit: return");
  }else if(screen==Marks){
    header("Bookmarks",marksReturn==ReadMenu?bookTitle():"Your saved passages");
    int pitch=69,visible=std::max(1,(height-120)/pitch),start=(marksRow/visible)*visible;
    if(savedMarks.empty())wrapped("No bookmarks yet. Open the reading menu to save a passage.",20,115,1,width-40,3,26);
    for(int i=start;i<int(savedMarks.size())&&i<start+visible;i++){
      int y=79+(i-start)*pitch;auto &mark=savedMarks[i];box(18,y-4,width-36,pitch-5,i==marksRow);label(mark.label,30,y+17,1,width-60);label(mark.passage,30,y+40,4,width-60);}
    footer("Center: open passage    Exit: return");
  }else if(screen==Gallery){
    header("Images",sdReady?std::to_string(gallery.size())+" pictures · On your microSD":"microSD is not available");
    if(gallery.empty())wrapped(sdReady?"Add JPG or PNG pictures with the PC book manager, then rescan in Settings.":"Insert your microSD card and restart to see your pictures.",20,112,1,width-40,4,26);
    else{int visible=height<width?2:3,pitch=height<width?79:89,start=(galleryRow/visible)*visible;
      for(int index=start;index<int(gallery.size())&&index<start+visible;index++){int y=78+(index-start)*pitch;box(18,y,width-36,pitch-7,index==galleryRow);
        if(!galleryImage(gallery[index],canvas,width,height,26,y+6,60,pitch-19)){box(26,y+6,60,pitch-19);label("?",48,y+39,1,20);}
        std::string name=gallery[index].substr(gallery[index].find_last_of('/')+1);wrapped(name,98,y+27,1,width-126,2,23);}
      label(std::to_string(galleryRow+1)+" / "+std::to_string(gallery.size()),width-88,height-40,4,70);}
    footer("Center: view    Exit: dashboard");
  }else if(screen==Picture){
    if(gallery.empty()||!galleryImage(gallery[galleryRow],canvas,width,height,0,0,width,height)){header("Image unavailable");wrapped("This image is corrupt, unsupported, or too large. Upload a display-sized copy using File Transfer.",20,112,1,width-40,5,26);footer("Up / Down: browse    Exit: gallery");}
  }else if(screen==PictureMenu){
    header("Picture",gallery.empty()?"":gallery[galleryRow].substr(gallery[galleryRow].find_last_of('/')+1));listRows({"Return to gallery","Rotation: "+std::to_string(storage.settings.rotation)+"°","Sleep"},pictureMenuRow);footer("Center: select    Exit: picture");
  }else if(screen==Transfer){
    label("File Transfer",16,25,1,width-32);
    if(transfer.total()&&transfer.busy()){
      label(transfer.message(),16,65,1,width-32);wrapped(transfer.filename().substr(transfer.filename().find_last_of('/')+1),16,104,1,width-32,3,25);
      int percent=int(uint64_t(transfer.completed())*100/transfer.total());box(16,height/2+25,width-32,18);if(percent)EPD_DrawRectangle(19,height/2+28,19+(width-38)*percent/100,height/2+39,BLACK,1);label(std::to_string(percent)+"%",16,height/2+75,1,width-32);
    }else{
      int qr=width>height?210:230,qrX=width>height?width-qr-8:(width-qr)/2,qrY=width>height?40:55;
      readerTransferQR(transferWebsiteQR?"http://192.168.4.1/":"WIFI:T:WPA;S:"+transfer.ssid()+";P:"+transfer.password()+";;",qrX,qrY,qr,[](int x,int y){Paint_SetPixel(x,y,BLACK);});
      int tx=16,ty=width>height?83:height-93,tw=width>height?width-qr-26:width-32;
      label(transfer.ssid(),tx,ty,1,tw);label("Pass: "+transfer.password(),tx,ty+25,4,tw);label("http://192.168.4.1",tx,ty+49,4,tw);
      if(width>height)wrapped(transfer.connected()?"Device connected":"Scan to join Wi-Fi",tx,ty+83,4,tw,2,18);
    }
    footer("Menu: QR toggle   Exit: return");
  }else{
    header(sdReady?"Unable to open this book":"microSD unavailable");wrapped(errorText,20,112,1,width-40,5,26);footer("Exit: return    Center: return");
  }
  rotateCanvas();
  if(panelReady&&displayHealthy&&lastPresented&&havePresented&&!forceFullRefresh&&!(screen==Clock&&readerClock.cleanupDue())&&!memcmp(frame,lastPresented,sizeof(frame))&&!lastDisplayWasSmooth){
    if(screen==Reading)savePosition();if(screen!=Transfer&&screen!=Clock&&screen!=FontLab)storage.saveDocument("session.json",sessionText());
    Serial.printf("RENDER mode=%s refresh=unchanged rotation=%d chapter=%d page=%d offset=%u ms=0\n",screenName(),storage.settings.rotation,chapter,page,currentOffset());dirty=false;return;
  }
  uint32_t start=millis();bool full=forceFullRefresh||!panelReady||lastDisplayWasSmooth||(screen==Clock?readerClock.cleanupDue():storage.settings.fullRefreshEvery==0||partialCount>=unsigned(storage.settings.fullRefreshEvery));
  EPD_ClearError();
  if(full){EPD_Init();EPD_Display(frame);panelReady=true;partialCount=0;}else{EPD_Display_Part(0,0,400,300,frame);++partialCount;}
  if(EPD_HasError()&&!full){Serial.println("DISPLAY partial failed; full recovery");EPD_ClearError();EPD_Init();EPD_Display(frame);partialCount=0;full=true;}
  displayHealthy=!EPD_HasError();if(!displayHealthy){panelReady=false;dirty=false;Serial.println("DISPLAY failed; sleep disabled until successful refresh");return;}
  {EPD_Address_Set(0,0,399,299);EPD_SetCursor(0,0);EPD_WR_REG(0x26);for(size_t i=0;i<sizeof(frame);i++)EPD_WR_DATA8(frame[i]);}
  lastDisplayWasSmooth=false;
  if(lastPresented){memcpy(lastPresented,frame,sizeof(frame));havePresented=true;}forceFullRefresh=false;if(screen==Clock)readerClock.presented(full);
  if(screen==Reading)savePosition();if(screen!=Transfer&&screen!=Clock&&screen!=FontLab)storage.saveDocument("session.json",sessionText());
  Serial.printf("RENDER mode=%s refresh=%s rotation=%d chapter=%d page=%d offset=%u ms=%lu heap=%u psram=%u\n",screenName(),full?"full":"partial",storage.settings.rotation,chapter,page,currentOffset(),(unsigned long)(millis()-start),ESP.getFreeHeap(),ESP.getFreePsram());dirty=false;
}
static void leaveGrayProbe(){
  grayProbeActive=false;forceFullRefresh=true;dirty=true;changedAt=millis();lastInteraction=millis();
  // The ordinary driver reloads its OTP waveform on the mandatory full refresh.
}
static void enterGrayProbe(){
  if(grayProbeActive||screen==Transfer||screen==Clock||screen==FontLab)return;
  savePosition();storage.saveDocument("session.json",sessionText());sleepRequested=false;
  grayProbeActive=readerGrayProbePresent(storage.settings.rotation,grayProbeMs);
  grayProbeSince=millis();dirty=false;
  Serial.printf("GRAY_PROBE transport_pass=%d optical=unverified rotation=%d ms=%u\n",grayProbeActive,storage.settings.rotation,grayProbeMs);
  if(!grayProbeActive)leaveGrayProbe();
}
static void enterFontLab(){
  if(screen==FontLab||screen==Transfer)return;
  readerClock.leave();savePosition();fontLabSession=sessionText();storage.saveDocument("session.json",fontLabSession);
  fontLabReturn=screen;screen=FontLab;inSettings=false;sleepRequested=false;fontLab.reset();
  forceFullRefresh=true;dirty=true;changedAt=millis();
  Serial.println("FONTLAB bank=0 style=1 baseline=0");
}
static void enterClock(bool synchronize){
#if READER_ENABLE_CLOCK_SCREENSAVER
  if(screen==Clock)return;savePosition();clockReturn=screen;screen=Clock;inSettings=false;sleepRequested=false;
  readerClock.enter(synchronize);storage.saveDocument("session.json",sessionText());dirty=true;changedAt=millis();
#endif
}
static void enterSettings(Screen previous){settingsAnchor=currentOffset();returnScreen=previous;screen=Settings;inSettings=true;advancedSettings=false;settingsRow=0;}
static void changeSetting(int row){
  if(row==15){enterFontLab();return;}
  if(row==14){enterClock();return;}
  if(row==12||row==13){advancedSettings=row==12;settingsRow=advancedSettings?0:9;return;}
  auto &cfg=storage.settings;
  if(row==11)cfg.fontFamily=(cfg.fontFamily+1)%4;
  if(row==16){cfg.smoothText=false;Serial.println("TEXT monochrome only");}
  if(row==0){fontIndex=(fontIndex+1)%4;cfg.fontPx=fontSizes[fontIndex];}
  if(row==1)cfg.rotation=(cfg.rotation+90)%360;
  if(row==2)cfg.margin=cfg.margin==12?20:cfg.margin==20?28:12;
  if(row==3)cfg.lineGap=cfg.lineGap==3?5:cfg.lineGap==5?8:3;
  if(row==4)cfg.showTitle=!cfg.showTitle;if(row==5)cfg.showProgress=!cfg.showProgress;
  if(row==6){const int values[]={4,8,16,0};int i=0;while(i<4&&values[i]!=cfg.fullRefreshEvery)++i;cfg.fullRefreshEvery=values[(i+1)%4];}
  if(row==7)cfg.development=!cfg.development;
  if(row==8){libraryScan();return;}
  if(row==9){screen=returnScreen;inSettings=false;sleepRequested=true;return;}
  if(row==10){enterTransfer();return;}
  storage.saveSettings();if(inBook&&(row<=5||row==11))paginate(settingsAnchor);
  Serial.printf("SETTINGS font=%d rotation=%d margin=%d spacing=%d development=%d anchor=%u actual=%u\n",cfg.fontPx,cfg.rotation,cfg.margin,cfg.lineGap,cfg.development,settingsAnchor,currentOffset());
}
static void addBookmark(){
  if(!book||demo){toast="Bookmarks need a book on microSD";return;}
  uint32_t offset=currentOffset();size_t cursor=offset,end=offset;
  while(cursor<chapterText.size()){nextCp(chapterText,cursor);if(cursor-offset>220)break;end=cursor;}
  std::string passage=chapterText.substr(offset,end-offset);
  size_t nl=passage.find('\n');if(nl!=std::string::npos)passage.resize(nl);
  std::string label=chapterName()+" · "+std::to_string(offset);
  bool removing=false;for(auto &mark:storage.namedBookmarks(book->get_path()))if(mark.chapter==uint32_t(chapter)&&mark.offset==offset)removing=true;
  if(!pages.empty()&&lines[pages[page].first].image>=0)passage="Image page";
  if(storage.toggleNamedBookmark({book->get_path(),label,passage,uint32_t(chapter),offset}))toast=removing?"Bookmark removed":"Bookmark saved";else toast="Could not save; check microSD";
}
static void openPath(const std::string &path){int index=bookIndex(path);if(index<0){notice("This book is no longer on the card.",Dashboard);return;}selected=index;openSelected();if(!inBook)notice(errorText.empty()?"This EPUB cannot be opened. DRM-protected books are unsupported.":errorText,Library);}
static void enterTransfer(){
  if(screen==Transfer)return;readerClock.leave();savePosition();transferSession=sessionText();storage.saveDocument("session.json",transferSession);book.reset();inBook=false;transferWebsiteQR=false;transfer.setCancellationProbe([](){return digitalRead(buttonPins[1])==LOW;});transfer.start(sdReady);screen=Transfer;inSettings=false;sleepRequested=false;dirty=true;changedAt=millis();transferRenderedAt=0;transferRenderedPosition=0;transferRenderedConnection=false;transferRenderedBusy=false;
  if(!transfer.active()){leaveTransfer();notice("Could not start reader Wi-Fi. Restart the reader and try File Transfer again.",screen==Clock?clockReturn:screen);}
}
static void leaveTransfer(){
  bool modified=transfer.changed();transfer.stop();if(modified)invalidateReaderCaches();libraryScan();bool ready=panelReady;panelReady=false;bool restored=restoreSession(transferSession);panelReady=ready;
  if(!restored)notice("The previous book is no longer available. Choose a book from Library.",Dashboard);transferSession.clear();lastInteraction=millis();dirty=true;changedAt=millis();
}
static void action(uint8_t key){
  lastInteraction=millis();Serial.printf("ACTION key=%u mode=%s\n",key,screenName());toast.clear();
  if(grayProbeActive){leaveGrayProbe();xQueueReset(inputQueue);return;}
  if(screen==FontLab){
    if(key==1){screen=fontLabReturn;inSettings=screen==Settings;fontLabSession.clear();}
    else if(key==0)fontLab.toggleBank();
    else if(key==2||key==3)fontLab.move(key==2?1:-1);
    else if(key==4)fontLab.toggleReference();
    Serial.printf("FONTLAB bank=%d style=%d baseline=%d\n",fontLab.bank(),fontLab.index()+1,fontLab.reference());
    forceFullRefresh=true;dirty=true;changedAt=millis();return;
  }
  if(screen==Transfer){if(key==1)leaveTransfer();else if(key==0){transferWebsiteQR=!transferWebsiteQR;dirty=true;changedAt=millis();}return;}
  if(screen==Clock){
    if(!readerClock.valid()&&!readerClock.syncing()&&key==0)readerClock.retry();
    else{readerClock.leave();screen=clockReturn;inSettings=screen==Settings;}
    dirty=true;changedAt=millis();return;
  }
  if(screen==Settings){
    if(key==0||key==1){if(advancedSettings){advancedSettings=false;settingsRow=9;}else{screen=returnScreen;inSettings=false;}}
    if(key==2)settingsRow=readerWrapSelection(settingsRow,1,readerSettingsCount(advancedSettings));if(key==3)settingsRow=readerWrapSelection(settingsRow,-1,readerSettingsCount(advancedSettings));if(key==4)changeSetting(readerSettingId(advancedSettings,settingsRow));
  }else if(screen==Dashboard){
    if(key==2)dashboardRow=(dashboardRow+1)%5;if(key==3)dashboardRow=(dashboardRow+4)%5;if(key==0)enterSettings(Dashboard);
    if(key==4){if(dashboardRow==0){if(inBook)screen=Reading;else if(!storage.lastBook().empty())openPath(storage.lastBook());else screen=Library;}
      if(dashboardRow==1)screen=Library;if(dashboardRow==2){marksReturn=Dashboard;refreshMarks(false);screen=Marks;}if(dashboardRow==3)screen=Gallery;if(dashboardRow==4)enterSettings(Dashboard);}
  }else if(screen==Library){
    if(key==1)screen=Dashboard;if(key==0)enterSettings(Library);
    if(key==2)selected=(selected+1)%(books.size()+1);if(key==3)selected=(selected+books.size())%(books.size()+1);
    if(key==4){inBook=false;openSelected();if(!inBook)notice(errorText.empty()?"This EPUB cannot be opened. DRM-protected books are unsupported.":errorText,Library);}
  }else if(screen==Reading){
    if(key==0){savePosition();screen=ReadMenu;menuRow=0;}
    if(key==1){savePosition();screen=Dashboard;}
    if(key==2||key==4){if(page+1<pageCount())++page;else if(!demo&&chapter+1<book->get_spine_items_count())loadChapter(chapter+1);}
    if(key==3){if(page>0)--page;else if(!demo&&chapter>0&&loadChapter(chapter-1))page=pageCount()-1;}
  }else if(screen==ReadMenu){
    if(key==0||key==1)screen=Reading;if(key==2)menuRow=readerWrapSelection(menuRow,1,readerMenuCount());if(key==3)menuRow=readerWrapSelection(menuRow,-1,readerMenuCount());
    if(key==4){if(menuRow==0){refreshToc();screen=Chapters;}if(menuRow==1)addBookmark();if(menuRow==2){marksReturn=ReadMenu;refreshMarks(true);screen=Marks;}
      if(menuRow==3)enterSettings(Reading);if(menuRow==4)screen=Dashboard;if(menuRow==5){screen=Reading;sleepRequested=true;}if(menuRow==6)enterClock();}
  }else if(screen==Chapters){
    int count=book?(tocItems.empty()?book->get_spine_items_count():tocItems.size()):0;
    if(key==1||key==0)screen=ReadMenu;if(count){if(key==2)chapterRow=(chapterRow+1)%count;if(key==3)chapterRow=(chapterRow+count-1)%count;
      if(key==4){int index=tocItems.empty()?chapterRow:book->get_spine_index_for_toc_index(tocItems[chapterRow]);std::string anchor=tocItems.empty()?"":book->get_toc_item(tocItems[chapterRow]).anchor;
        if(loadChapter(index)){if(!anchor.empty()&&htmlAnchors.count(anchor)){uint32_t at=htmlAnchors[anchor];while(at<chapterText.size()&&isspace((unsigned char)chapterText[at]))++at;paginate(at);}screen=Reading;}else notice(errorText,Chapters);}}
  }else if(screen==Marks){
    if(key==1||key==0)screen=marksReturn;int count=savedMarks.size();if(count){if(key==2)marksRow=(marksRow+1)%count;if(key==3)marksRow=(marksRow+count-1)%count;
      if(key==4){NamedBookmark mark=savedMarks[marksRow];openPath(mark.path);if(inBook&&book&&loadChapter(mark.chapter)){paginate(mark.offset);screen=Reading;}}}
  }else if(screen==Gallery){
    if(key==1)screen=Dashboard;if(key==0)enterSettings(Gallery);if(!gallery.empty()){if(key==2)galleryRow=(galleryRow+1)%gallery.size();if(key==3)galleryRow=(galleryRow+gallery.size()-1)%gallery.size();if(key==4)screen=Picture;}
  }else if(screen==Picture){
    if(key==1)screen=Gallery;if(key==0){screen=PictureMenu;pictureMenuRow=0;}if(!gallery.empty()){if(key==2||key==4)galleryRow=(galleryRow+1)%gallery.size();if(key==3)galleryRow=(galleryRow+gallery.size()-1)%gallery.size();}
  }else if(screen==PictureMenu){
    if(key==0||key==1)screen=Picture;if(key==2)pictureMenuRow=(pictureMenuRow+1)%3;if(key==3)pictureMenuRow=(pictureMenuRow+2)%3;
    if(key==4){if(pictureMenuRow==0)screen=Gallery;if(pictureMenuRow==1){settingsAnchor=currentOffset();changeSetting(1);}if(pictureMenuRow==2){screen=Picture;sleepRequested=true;}}
  }else if(key==0||key==1||key==4)screen=noticeReturn;
  dirty=true;changedAt=millis();
}
static void physicalAction(uint8_t key){
  if(key>4)return;uint8_t logical=readerLogicalButton(key,storage.settings.rotation);
  Serial.printf("BUTTON gpio=%u rotation=%d logical=%u\n",buttonPins[key],storage.settings.rotation,logical);action(logical);
}
static void shutdownPeripherals(){if(inputTask){vTaskDelete(inputTask);inputTask=nullptr;}SD.end();cardSPI.end();}
static bool readyForSleep(){if(uxQueueMessagesWaiting(inputQueue)||Serial.available())return false;for(uint8_t pin:buttonPins)if(digitalRead(pin)==LOW)return false;return !dirty&&displayHealthy;}
static void sleepNow(){
  if(!sdReady){sleepRequested=false;toast="Sleep recovery needs a microSD card";Serial.println("SLEEP refused=no_card");return;}
  savePosition();std::string text=sessionText();storage.saveDocument("session.json",text);
  if(!readerEnterSleep(storage,frame,text,partialCount,shutdownPeripherals,sleepTimer,sleepInjected,readyForSleep))Serial.println("SLEEP deferred=held_button_or_snapshot_failure");
  sleepRequested=false;sleepTimer=0;sleepInjected=255;
}
static void state(){Serial.println("FIRMWARE " INK_READER_VERSION);Serial.printf("STATE sd=%d books=%u selected=%d reading=%d settings=%d mode=%s row=%d chapter=%d page=%d pages=%d offset=%u font=%d rotation=%d size=%dx%d cleanup=%d partial=%u development=%d margin=%d gap=%d family=%d settings_count=%d menu_count=%d heap=%u psram=%u\n",sdReady,unsigned(books.size()),selected,screen==Reading,screen==Settings,screenName(),settingsRow,chapter,page,pageCount(),currentOffset(),fontSizes[fontIndex],storage.settings.rotation,viewW(),viewH(),storage.settings.fullRefreshEvery,partialCount,storage.settings.development,storage.settings.margin,storage.settings.lineGap,storage.settings.fontFamily,readerSettingsCount(advancedSettings),readerMenuCount(),ESP.getFreeHeap(),ESP.getFreePsram());}
void setup(){
  readerClock.begin();
  uint32_t bootAt=millis();bool woke=readerWokeFromSleep();uint8_t wakeKey=readerWakeButton();readerReleaseSleepHolds();
  Serial.setRxBufferSize(8192);Serial.begin(115200);if(!woke)delay(350);
  for(uint8_t pin:buttonPins)pinMode(pin,INPUT_PULLUP);
  pinMode(7,OUTPUT);digitalWrite(7,HIGH);pinMode(41,OUTPUT);digitalWrite(41,HIGH);delay(50);EPD_GPIOInit();
  pinMode(42,OUTPUT);digitalWrite(42,HIGH);delay(20);cardSPI.begin(39,13,40,10);
  sdReady=SD.begin(10,cardSPI,4000000,"/sd",12,false);if(!sdReady){SD.end();sdReady=SD.begin(10,cardSPI,1000000,"/sd",12,false);}
  lastPresented=(uint8_t *)ps_malloc(sizeof(frame));
  transfer.recover(sdReady);
  storage.begin(sdReady);
  if(woke&&wakeKey==254){storage.settings.development=true;storage.saveSettings();Serial.println("AUTO_SLEEP_TEST restored USB development mode");}
  for(int i=0;i<4;i++)if(fontSizes[i]==storage.settings.fontPx)fontIndex=i;libraryScan();
  bool recovered=false;
  if(woke&&sdReady){std::string text=readerSleepSession(storage);recovered=!text.empty()&&restoreSession(text)&&sessionFrameCompatible&&readerRestoreFrame(frame,partialCount);}
  if(!woke){std::string text=storage.loadDocument("session.json");if(!restoreSession(text)){screen=Dashboard;inSettings=false;}}
  if(recovered){
    EPD_Init();for(uint8_t command:{uint8_t(0x24),uint8_t(0x26)}){EPD_Address_Set(0,0,399,299);EPD_SetCursor(0,0);EPD_WR_REG(command);for(size_t i=0;i<sizeof(frame);i++)EPD_WR_DATA8(frame[i]);}recovered=!EPD_HasError();panelReady=recovered;dirty=!recovered;
  }
  if(recovered&&lastPresented){memcpy(lastPresented,frame,sizeof(frame));havePresented=true;lastDisplayWasSmooth=false;}
  inputQueue=xQueueCreate(32,sizeof(uint8_t));if(!inputQueue||xTaskCreate(scanInputs,"buttons",3072,nullptr,1,&inputTask)!=pdPASS){message("Input task failed");while(true)delay(1000);}
  if(woke&&wakeKey<5)physicalAction(wakeKey);
  if(!recovered||dirty)render();lastInteraction=millis();
  Serial.printf("READY wake=%d recovery=%d button=%u boot_ms=%lu free_psram=%u\n",woke,recovered,wakeKey,(unsigned long)(millis()-bootAt),ESP.getFreePsram());state();
}
static void pdfBookSelfTest(){
  if(!sdReady){Serial.println("SELFTEST pdf_book pass=0 missing_card=1");return;}
  const char *path="/.crowreader/validation-pdf.epub";
  if(SD.exists(path)){Serial.println("SELFTEST pdf_book pass=0 existing_fixture=1");return;}
  File out=SD.open(path,FILE_WRITE);bool ok=out&&out.write(fixturePdfBook,sizeof(fixturePdfBook))==sizeof(fixturePdfBook);out.flush();out.close();
  Epub sample(std::string("/sd")+path);ok=ok&&sample.load()&&sample.is_fixed_layout()&&sample.get_spine_items_count()==2&&sample.get_toc_items_count()==2;
  size_t length=0;uint8_t *image=ok?sample.get_item_contents(sample.get_cover_image_item(),&length):nullptr;
  uint8_t *pixels=(uint8_t *)heap_caps_malloc(sizeof(canvas),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  ok=ok&&image&&pixels;
  if(ok)for(int rotation:{0,90,180,270}){int w=rotation%180?300:400,h=rotation%180?400:300;memset(pixels,255,sizeof(canvas));bool decoded=renderBookImage(pixels,image,length,0,0,w,h,w,h);unsigned ink=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(!(pixels[y*((w+7)/8)+x/8]&(128>>(x&7))))++ink;bool pass=decoded&&ink>500;ok=ok&&pass;Serial.printf("PDF_IMAGE_TEST rotation=%d decoded=%d ink=%u pass=%d\n",rotation,decoded,ink,pass);}
  free(image);free(pixels);SD.remove(path);Serial.printf("SELFTEST pdf_book pass=%d\n",ok);
}
static void layoutSelfTest(){
  if(!inBook){Serial.println("SELFTEST no_book");return;}ReaderSettings original=storage.settings;int originalFi=fontIndex,originalPage=page;uint32_t anchor=currentOffset(),pivot=forcedBoundary;
  int failures=0,total=0;for(int fi=0;fi<4;fi++)for(int rotation:{0,90,180,270}){
    fontIndex=fi;storage.settings.fontPx=fontSizes[fi];storage.settings.rotation=rotation;paginate(anchor);bool ok=currentOffset()==anchor;
    if(page>0){--page;++page;ok=ok&&currentOffset()==anchor;}if(!ok)++failures;++total;
    Serial.printf("ANCHOR_TEST font=%d rotation=%d expected=%u actual=%u page=%d pass=%d\n",fontSizes[fi],rotation,anchor,currentOffset(),page,ok);
  }
  storage.settings=original;fontIndex=originalFi;paginate(pivot);restoreOffset(anchor);(void)originalPage;
  Serial.printf("SELFTEST anchor total=%d failures=%d\n",total,failures);
}
static void contentSelfTest(){
  const char *inputs[]={"\xD7\x90","\xF0\x9F\x93\x96","\xED\xA0\x80","\xF4\x90\x80\x80","\xE0\x80\x80","\xC0\xAF","\xE2"};
  const uint32_t expected[]={0x5D0,0x1F4D6,0xFFFD,0xFFFD,0xFFFD,0xFFFD,0xFFFD};
  bool utf=true;for(int i=0;i<7;i++){size_t at=0;utf=utf&&readerNextCodepoint(inputs[i],at)==expected[i]&&at>0;}
  Serial.printf("SELFTEST unicode scalar/overlong/truncated pass=%d\n",utf);
  if(!inBook){Serial.println("SELFTEST content no_book");return;}
  ReaderSettings original=storage.settings;int originalFi=fontIndex;uint32_t saved=currentOffset(),pivot=forcedBoundary;
  std::vector<uint32_t> anchors={saved};size_t at=0;
  while(anchors.size()<5&&(at=chapterText.find("[[IMG:",at))!=std::string::npos){anchors.push_back(at);at+=6;}
  for(const auto &block:blocks){uint32_t a=block.start;while(a<chapterText.size()&&isspace((unsigned char)chapterText[a]))++a;if(a<chapterText.size())anchors.push_back(a);if(anchors.size()>=10)break;}
  at=0;while(at<chapterText.size()/2)nextCp(chapterText,at);
  while(at<chapterText.size()&&isspace((unsigned char)chapterText[at]))++at;if(at<chapterText.size())anchors.push_back(at);
  std::sort(anchors.begin(),anchors.end());anchors.erase(std::unique(anchors.begin(),anchors.end()),anchors.end());
  int total=0,failed=0,imageCases=0;
  for(uint32_t anchor:anchors)for(int family=0;family<4;++family)for(int fi=0;fi<4;++fi)for(int rotation:{0,90,180,270}){
    storage.settings.fontFamily=family;storage.settings.fontPx=fontSizes[fi];fontIndex=fi;storage.settings.rotation=rotation;paginate(anchor);
    bool ok=currentOffset()==anchor;const auto &entry=pages[page];
    if(page>0)ok=ok&&pages[page-1].anchor<anchor;if(page+1<pageCount())ok=ok&&pages[page+1].anchor>anchor;
    if(lines[entry.first].image>=0){++imageCases;ok=ok&&entry.count==1&&chapterText.compare(anchor,6,"[[IMG:")==0;}
    if(!ok)++failed;++total;
  }
  storage.settings=original;fontIndex=originalFi;paginate(pivot);restoreOffset(saved);
  Serial.printf("SELFTEST content total=%d images=%d failures=%d restored=%d\n",total,imageCases,failed,currentOffset()==saved);
}
static void imageSelfTest(){
  uint8_t pixels[512];bool ok=imageQualitySelfTest();struct Fixture{const uint8_t *data;size_t size;const char *name;};
  Fixture fixtures[]={{fixtureProgressiveJpeg,sizeof(fixtureProgressiveJpeg),"progressive JPEG"},{fixtureAlphaPng,sizeof(fixtureAlphaPng),"transparent PNG"},{fixtureWidePng,sizeof(fixtureWidePng),"16-bit PNG"}};
  for(auto &f:fixtures){memset(pixels,255,sizeof(pixels));bool decoded=renderBookImage(pixels,const_cast<uint8_t *>(f.data),f.size,0,0,64,64,64,64);
    bool margins=true;for(int i=0;i<8*15;i++)if(pixels[i]!=255)margins=false;
    bool ink=false;for(size_t i=0;i<sizeof(pixels);i++)if(pixels[i]!=255)ink=true;
    bool white=pixels[16*8]==255;bool pass=decoded&&margins&&ink&&white;ok=ok&&pass;Serial.printf("IMAGE_TEST kind=%s decode=%d aspect=%d ink=%d pass=%d\n",f.name,decoded,margins,ink,pass);}
  uint8_t invalid[10]={0};memset(pixels,255,sizeof(pixels));bool rejected=!renderBookImage(pixels,invalid,sizeof(invalid),0,0,64,64,64,64);ok=ok&&rejected;
  memset(pixels,255,sizeof(pixels));bool bounded=!renderBookImage(pixels,const_cast<uint8_t *>(fixtureOversizedPng),sizeof(fixtureOversizedPng),0,0,64,64,64,64);ok=ok&&bounded;
  Serial.printf("SELFTEST images pass=%d invalid_rejected=%d allocation_guard=%d\n",ok,rejected,bounded);
}
static void epubSelfTest(){
  bool all=true;for(int version:{2,3}){
    std::string path="/.crowreader/validation-nav.epub";SD.remove(path.c_str());File f=SD.open(path.c_str(),FILE_WRITE);
    const uint8_t *data=version==2?fixtureEpub2:fixtureEpub3;size_t size=version==2?sizeof(fixtureEpub2):sizeof(fixtureEpub3);
    bool ok=f&&f.write(data,size)==size;f.flush();f.close();Epub epub("/sd"+path);
    ok=ok&&epub.load()&&epub.get_title()=="Fixture book"&&epub.get_author()=="Reader tests"&&epub.get_spine_items_count()==2&&epub.get_toc_items_count()==3;
    if(ok)ok=epub.get_spine_index_for_toc_index(0)==0&&epub.get_spine_index_for_toc_index(1)==0&&epub.get_spine_index_for_toc_index(2)==1&&epub.get_toc_item(1).anchor=="middle"&&epub.get_toc_item(1).level==1;
    all=all&&ok;SD.remove(path.c_str());Serial.printf("EPUB_TEST version=%d nested_toc/namespace/relative_path/fragment pass=%d\n",version,ok);
  }Serial.printf("SELFTEST epub pass=%d\n",all);
}
static std::string serialJump;
static bool collectingJump=false;
static void jumpTo(const std::string &text){
  if(!book||demo)return;char *end=nullptr;unsigned long ch=strtoul(text.c_str(),&end,10);if(!end||*end!=',')return;
  unsigned long offset=strtoul(end+1,&end,10);if(!end||*end||ch>=unsigned(book->get_spine_items_count()))return;
  if(int(ch)!=chapter&&!loadChapter(ch))return;if(offset>=chapterText.size())return;paginate(offset);screen=Reading;inSettings=false;dirty=true;
  Serial.printf("JUMP chapter=%lu expected=%lu actual=%u\n",ch,offset,currentOffset());
}
void loop(){
  uint8_t key;while(xQueueReceive(inputQueue,&key,0)==pdTRUE)physicalAction(key);
  if(screen==Transfer){
    transfer.poll();if(transfer.ended())leaveTransfer();
    else if(uint32_t(millis()-transferRenderedAt)>=5000&&(transferRenderedPosition!=transfer.completed()||transferRenderedConnection!=transfer.connected()||transferRenderedBusy!=transfer.busy())){dirty=true;changedAt=millis()-150;}
  }
  while(Serial.available()){
    char c=Serial.read();lastInteraction=millis();
    if(grayProbeActive){
      if(c=='s'){state();Serial.printf("GRAY active=1 optical=unverified ms=%u remaining_ms=%u\n",grayProbeMs,300000u-std::min<uint32_t>(300000,uint32_t(millis()-grayProbeSince)));}
      else if(c=='x'){leaveGrayProbe();Serial.flush();ESP.restart();}
      else if(c=='b'||c=='f'||c=='n'||c=='p'||c=='o'||c=='U'||c=='D')action(1);
      continue;
    }
    if(c=='^'){enterGrayProbe();continue;}
    if(screen==FontLab&&c!='n'&&c!='p'&&c!='o'&&c!='b'&&c!='f'&&c!='U'&&c!='D'&&c!='s'&&c!='d'&&c!='x'&&c!='v')continue;
    if(c=='v')enterFontLab();
    if(screen==Transfer){
      if(c=='x'){transfer.stop();Serial.flush();ESP.restart();}
      if(c!='b'&&c!='f'&&c!='s'&&c!='d'&&c!='R'&&c!='E'&&c!='U'&&c!='D')continue;
    }
    int64_t previewEpoch=0;int clockInput=ReaderClock::previewCharacter(c,previewEpoch);
    if(clockInput){if(clockInput==2){enterClock(false);readerClock.previewAt(previewEpoch);dirty=true;}continue;}
    if(c=='w')enterClock();if(c=='~')ReaderClock::selfTest();
    if(c=='!'){collectingJump=false;serialJump.clear();enterTransfer();break;}
    if(collectingJump){if(c=='\n'){collectingJump=false;jumpTo(serialJump);serialJump.clear();}else if(c!='\r'&&serialJump.size()<40)serialJump+=c;continue;}
    if(c=='@'){collectingJump=true;serialJump.clear();continue;}
    if(c=='n')action(2);if(c=='p')action(3);if(c=='o')action(4);if(c=='b')action(1);if(c=='f')action(0);if(c=='U')physicalAction(3);if(c=='D')physicalAction(2);
    if(c=='B'&&inBook){screen=Reading;inSettings=false;dirty=true;}if(c=='h'){screen=Dashboard;inSettings=false;dirty=true;}if(c=='l'){screen=Library;inSettings=false;dirty=true;}
    if(c=='e')enterSettings(inBook?Reading:Dashboard);
    if(c=='['){savePosition();bool ok=readerValidationBegin(storage,sessionText());Serial.printf("QA_BEGIN pass=%d\n",ok);}
    if(c==']'){
      std::string restored;bool ok=readerValidationRestore(storage,restored);
      if(ok){book.reset();inBook=false;for(int i=0;i<4;++i)if(fontSizes[i]==storage.settings.fontPx)fontIndex=i;libraryScan();ok=restoreSession(restored);}
      if(ok){readerValidationComplete();dirty=true;}Serial.printf("QA_RESTORE pass=%d\n",ok);
    }
    if(c=='L'){libraryScan();dirty=true;}
    if(c=='g'){screen=Gallery;dirty=true;}
    if(c=='H')transfer.selfTest(sdReady);
    if(c=='F'||c=='R'||c=='G'||c=='K'){settingsAnchor=currentOffset();changeSetting(c=='F'?0:c=='R'?1:c=='G'?2:3);dirty=true;}
    if(c=='N'){settingsAnchor=currentOffset();changeSetting(11);dirty=true;}
    if(c=='Y')contentSelfTest();if(c=='M')pdfBookSelfTest();
    if(c=='E'){settingsAnchor=currentOffset();changeSetting(7);dirty=true;}
    if(c=='Q'){refreshToc();screen=Chapters;dirty=true;}if(c=='A'){addBookmark();Serial.printf("NAMED %s\n",toast.c_str());}
    if(c=='V'){marksReturn=inBook?ReadMenu:Dashboard;refreshMarks(inBook);screen=Marks;dirty=true;}
    if(c=='t'){selected=books.size();openSelected();dirty=true;}
    if(c=='c'&&inBook&&!demo){loadChapter(0);screen=Reading;dirty=true;}
    if(c=='r'){forceFullRefresh=true;dirty=true;}if(c=='j')storage.dumpFiles();if(c=='s'){state();transfer.diagnostics();Serial.printf("GALLERY images=%u selected=%d path=%s\n",unsigned(gallery.size()),galleryRow,gallery.empty()?"":gallery[galleryRow].c_str());Serial.printf("CLOCK active=%d valid=%d wifi=%d syncing=%d preview=%d minute=%lld\n",readerClock.active(),readerClock.valid(),readerClock.radioOn(),readerClock.syncing(),readerClock.preview(),(long long)readerClock.minute());}if(c=='T')layoutSelfTest();if(c=='I')storage.selfTest();if(c=='J')imageSelfTest();if(c=='P')epubSelfTest();if(c=='C'&&book)readerCacheSelfTest(*book,book->get_cover_image_item(),sdReady);
    if(c=='a'&&(screen==Reading||screen==Picture)){storage.settings.development=false;storage.saveSettings();sleepTimer=3;sleepInjected=254;dirty=true;Serial.println("AUTO_SLEEP_TEST enabled battery behavior for one timer cycle");}
    if(c=='Z'||c=='W'||c=='S'){sleepTimer=c=='S'?0:3;sleepInjected=c=='W'?2:255;sleepRequested=true;}
    if(c=='x'){savePosition();storage.saveDocument("session.json",sessionText());Serial.flush();ESP.restart();}
    if(c=='d'){Serial.println("FRAME_BEGIN");for(int y=0;y<300;y++){char row[101];const char hex[]="0123456789abcdef";for(int x=0;x<50;x++){uint8_t pixel=frame[y*50+x];row[x*2]=hex[pixel>>4];row[x*2+1]=hex[pixel&15];}row[100]=0;Serial.println(row);}Serial.println("FRAME_END");}
    if(c=='q')Serial.println("TEXT monochrome only; use d for framebuffer");
    if(c=='k'){settingsAnchor=currentOffset();changeSetting(16);forceFullRefresh=true;dirty=true;}
    if(c=='e')dirty=true;changedAt=millis();
  }
  if(readerClock.active()&&screen!=Clock)readerClock.leave();
  if(grayProbeActive&&uint32_t(millis()-grayProbeSince)>=300000)leaveGrayProbe();
  if(screen==Clock&&readerClock.tick()){dirty=true;changedAt=millis()-150;}
  if(dirty&&!grayProbeActive&&uint32_t(millis()-changedAt)>=150){render();if(screen==Transfer){transferRenderedAt=millis();transferRenderedPosition=transfer.completed();transferRenderedConnection=transfer.connected();transferRenderedBusy=transfer.busy();}}
  bool automatic=!grayProbeActive&&screen!=FontLab&&screen!=Clock&&screen!=Transfer&&!storage.settings.development&&(screen==Reading||screen==Picture||uint32_t(millis()-lastInteraction)>=60000);
  if(displayHealthy&&!dirty&&!grayProbeActive&&screen!=FontLab&&screen!=Clock&&(sleepRequested||automatic)&&uxQueueMessagesWaiting(inputQueue)==0&&!Serial.available()&&uint32_t(millis()-lastSleepAttempt)>=1000){lastSleepAttempt=millis();sleepNow();}
  delay(1);
}
