#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGE(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGD(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGW(tag, args...) \
  printf(args);                \
  printf("\n");
#endif
#include <map>
#include <functional>
#include "tinyxml2.h"
#include "ZipFile.h"
#include "Epub.h"

static const char *TAG = "EPUB";
std::string normalise_path(const std::string &path);
static std::string localName(const char *name){std::string s=name?name:"";size_t colon=s.find(':');return colon==std::string::npos?s:s.substr(colon+1);}
static tinyxml2::XMLElement *firstElement(tinyxml2::XMLNode *node,const char *name){if(!node)return nullptr;for(auto *p=node->FirstChildElement();p;p=p->NextSiblingElement())if(localName(p->Name())==name)return p;return nullptr;}
static tinyxml2::XMLElement *nextElement(tinyxml2::XMLElement *node,const char *name){if(!node)return nullptr;for(auto *p=node->NextSiblingElement();p;p=p->NextSiblingElement())if(localName(p->Name())==name)return p;return nullptr;}
static std::string nodeText(tinyxml2::XMLNode *node){
  if(!node)return "";if(node->ToText())return node->Value();
  std::string result;for(auto *child=node->FirstChild();child;child=child->NextSibling())result+=nodeText(child);return result;
}

bool Epub::find_content_opf_file(ZipFile &zip, std::string &content_opf_file)
{
  // open up the meta data to find where the content.opf file lives
  char *meta_info = (char *)zip.read_file_to_memory("META-INF/container.xml");
  if (!meta_info)
  {
    ESP_LOGE(TAG, "Could not find META-INF/container.xml");
    return false;
  }
  // parse the meta data
  tinyxml2::XMLDocument meta_data_doc;
  auto result = meta_data_doc.Parse(meta_info);
  // finished with the data as it's been parsed
  free(meta_info);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Could not parse META-INF/container.xml");
    return false;
  }
  auto container = firstElement(&meta_data_doc,"container");
  if (!container)
  {
    ESP_LOGE(TAG, "Could not find container element in META-INF/container.xml");
    return false;
  }
  auto rootfiles = firstElement(container,"rootfiles");
  if (!rootfiles)
  {
    ESP_LOGE(TAG, "Could not find rootfiles element in META-INF/container.xml");
    return false;
  }
  // find the root file that has the media-type="application/oebps-package+xml"
  auto rootfile = firstElement(rootfiles,"rootfile");
  while (rootfile)
  {
    const char *media_type = rootfile->Attribute("media-type");
    if (media_type && strcmp(media_type, "application/oebps-package+xml") == 0)
    {
      const char *full_path = rootfile->Attribute("full-path");
      if (full_path)
      {
        content_opf_file = full_path;
        return true;
      }
    }
    rootfile = nextElement(rootfile,"rootfile");
  }
  ESP_LOGE(TAG, "Could not get path to content.opf file");
  return false;
}

bool Epub::parse_content_opf(ZipFile &zip, std::string &content_opf_file)
{
  // read in the content.opf file and parse it
  char *contents = (char *)zip.read_file_to_memory(content_opf_file.c_str());
  // parse the contents
  tinyxml2::XMLDocument doc;
  if (!contents) return false;
  auto result = doc.Parse(contents);
  free(contents);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Error parsing content.opf - %s", doc.ErrorIDToName(result));
    return false;
  }
  auto package = firstElement(&doc,"package");
  if (!package)
  {
    ESP_LOGE(TAG, "Could not find package element in content.opf");
    return false;
  }
  // get the metadata - title and cover image
  auto metadata = firstElement(package,"metadata");
  if (!metadata)
  {
    ESP_LOGE(TAG, "Missing metadata");
    return false;
  }
  auto title = firstElement(metadata,"title");
  if (!title)
  {
    ESP_LOGE(TAG, "Missing title");
    return false;
  }
  m_title=nodeText(title);if(m_title.empty())m_title="Untitled";
  for(auto *child=metadata->FirstChildElement();child;child=child->NextSiblingElement())if(localName(child->Name())=="creator"){m_author=nodeText(child);break;}
  auto cover = firstElement(metadata,"meta");
  while (cover && (!cover->Attribute("name") || strcmp(cover->Attribute("name"), "cover") != 0))
  {
    cover = nextElement(cover,"meta");
  }
  if (!cover)
  {
    ESP_LOGW(TAG, "Missing cover");
  }
  auto cover_item = cover ? cover->Attribute("content") : nullptr;
  // read the manifest and spine
  // the manifest gives us the names of the files
  // the spine gives us the order of the files
  // we can then read the files in the order they are in the spine
  auto manifest = firstElement(package,"manifest");
  if (!manifest)
  {
    ESP_LOGE(TAG, "Missing manifest");
    return false;
  }
  // create a mapping from id to file name
  auto item = firstElement(manifest,"item");
  std::map<std::string, std::string> items;

  while (item)
  {
    if (!item->Attribute("id") || !item->Attribute("href")) { item = nextElement(item,"item"); continue; }
    std::string item_id = item->Attribute("id");
    std::string href = m_base_path + item->Attribute("href");
    // grab the cover image
    if ((cover_item && item_id == cover_item) ||
        (item->Attribute("properties") && strstr(item->Attribute("properties"), "cover-image")))
    {
      m_cover_image_item = href;
    }
    // grab the ncx file
    if (item_id == "ncx")
    {
      m_toc_ncx_item = href;
    }
    items[item_id] = href;
    const char *type=item->Attribute("media-type"),*properties=item->Attribute("properties");
    if(type&&!strcmp(type,"application/x-dtbncx+xml"))m_toc_ncx_item=href;
    if(properties&&strstr(properties,"nav"))m_nav_item=href;
    item = nextElement(item,"item");
  }
  // find the spine
  auto spine = firstElement(package,"spine");
  if (!spine)
  {
    ESP_LOGE(TAG, "Missing spine");
    return false;
  }
  // read the spine
  auto itemref = firstElement(spine,"itemref");
  while (itemref)
  {
    auto id = itemref->Attribute("idref");
    if (id && items.find(id) != items.end() && (!itemref->Attribute("linear") || strcmp(itemref->Attribute("linear"), "no") != 0))
    {
      m_spine.push_back(std::make_pair(id, items[id]));
    }
    itemref = nextElement(itemref,"itemref");
  }
  return true;
}

bool Epub::parse_nav_file(ZipFile &zip){
  if(m_nav_item.empty())return false;
  char *text=(char *)zip.read_file_to_memory(m_nav_item.c_str());if(!text)return false;
  tinyxml2::XMLDocument doc;auto result=doc.Parse(text);free(text);if(result!=tinyxml2::XML_SUCCESS)return false;
  std::string base=m_nav_item.substr(0,m_nav_item.find_last_of('/')+1);
  std::function<void(tinyxml2::XMLElement*,bool,int)> walk;
  walk=[&](tinyxml2::XMLElement *node,bool toc,int level){
    for(;node;node=node->NextSiblingElement()){
      std::string name=localName(node->Name());bool inside=toc;
      if(name=="nav"){const char *type=node->Attribute("epub:type"),*role=node->Attribute("role");inside=(type&&strstr(type,"toc"))||(role&&strstr(role,"doc-toc"));}
      if(inside&&name=="a"&&node->Attribute("href")){
        std::string href=base+node->Attribute("href"),anchor;size_t hash=href.find('#');
        if(hash!=std::string::npos){anchor=href.substr(hash+1);href.resize(hash);}
        m_toc.emplace_back(nodeText(node),normalise_path(href),anchor,std::max(0,level-1));
      }
      walk(node->FirstChildElement(),inside,level+(name=="ol"));
    }
  };
  walk(doc.RootElement(),false,0);return !m_toc.empty();
}

bool Epub::parse_toc_ncx_file(ZipFile &zip)
{
  // the ncx file should have been specified in the content.opf file
  if (m_toc_ncx_item.empty())
  {
    ESP_LOGE(TAG, "No ncx file specified");
    return false;
  }
  ESP_LOGI(TAG, "toc path: %s\n", m_toc_ncx_item.c_str());

  char *ncx_data = (char *)zip.read_file_to_memory(m_toc_ncx_item.c_str());
  if (!ncx_data)
  {
    ESP_LOGE(TAG, "Could not find %s", m_toc_ncx_item.c_str());
    return false;
  }
  // Parse the Toc contents
  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(ncx_data);
  free(ncx_data);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Error parsing toc %s", doc.ErrorIDToName(result));
    return false;
  }
  auto ncx = firstElement(&doc,"ncx");
  if (!ncx)
  {
    ESP_LOGE(TAG, "Could not find first child ncx in toc");
    return false;
  }

  auto navMap = firstElement(ncx,"navMap");
  if (!navMap)
  {
    ESP_LOGE(TAG, "Could not find navMap child in ncx");
    return false;
  }

  std::string base=m_toc_ncx_item.substr(0,m_toc_ncx_item.find_last_of('/')+1);
  std::function<void(tinyxml2::XMLElement*,int)> walk;
  walk=[&](tinyxml2::XMLElement *point,int level){
    for(;point;point=nextElement(point,"navPoint")){
      auto *label=firstElement(point,"navLabel"),*content=firstElement(point,"content");
      if(content&&content->Attribute("src")){
        std::string href=base+content->Attribute("src"),anchor;size_t hash=href.find('#');
        if(hash!=std::string::npos){anchor=href.substr(hash+1);href.resize(hash);}
        m_toc.emplace_back(label?nodeText(label):"Chapter",normalise_path(href),anchor,level);
      }
      walk(firstElement(point,"navPoint"),level+1);
    }
  };walk(firstElement(navMap,"navPoint"),0);
  return true;
}

Epub::Epub(const std::string &path) : m_path(path)
{
}

// load in the meta data for the epub file
bool Epub::load()
{
  m_title.clear();m_author.clear();m_spine.clear();m_toc.clear();m_nav_item.clear();m_toc_ncx_item.clear();m_cover_image_item.clear();
  std::string suffix=m_path.size()>=4?m_path.substr(m_path.size()-4):"";for(char &c:suffix)c=tolower((unsigned char)c);
  m_text=suffix==".txt";
  if(m_text){
    struct stat st{};if(stat(m_path.c_str(),&st)||st.st_size<=0||st.st_size>1024*1024)return false;
    m_title=m_path.substr(m_path.find_last_of('/')+1);m_title.resize(m_title.size()-4);m_author="Plain text";
    m_spine.push_back({"text","text"});m_toc.emplace_back(m_title,"text","",0);return true;
  }
  ZipFile zip(m_path.c_str());
  std::string content_opf_file;
  if (!find_content_opf_file(zip, content_opf_file))
  {
    return false;
  }
  // get the base path for the content
  m_base_path = content_opf_file.substr(0, content_opf_file.find_last_of('/') + 1);
  if (!parse_content_opf(zip, content_opf_file))
  {
    return false;
  }
  // NCX is optional in EPUB3; chapter reading uses the spine.
  if (m_spine.empty()) return false;
  if(!parse_nav_file(zip))parse_toc_ncx_file(zip);
  return true;
}

const std::string &Epub::get_title()
{
  return m_title;
}

const std::string &Epub::get_cover_image_item()
{
  return m_cover_image_item;
}

std::string normalise_path(const std::string &path){
  std::vector<std::string> parts;size_t start=0;
  while(start<=path.size()){size_t end=path.find('/',start);if(end==std::string::npos)end=path.size();std::string part=path.substr(start,end-start);
    if(part==".."){if(!parts.empty())parts.pop_back();}else if(!part.empty()&&part!=".")parts.push_back(part);
    if(end==path.size())break;start=end+1;
  }std::string result;for(auto &part:parts){if(!result.empty())result+='/';result+=part;}return result;
}

uint8_t *Epub::get_item_contents(const std::string &item_href, size_t *size)
{
  if(m_text){
    if(item_href!="text")return nullptr;
    FILE *f=fopen(m_path.c_str(),"rb");if(!f)return nullptr;
    if(fseek(f,0,SEEK_END)){fclose(f);return nullptr;}long length=ftell(f);rewind(f);
    if(length<=0||length>1024*1024){fclose(f);return nullptr;}
    uint8_t *result=(uint8_t*)malloc(length+1);if(!result){fclose(f);return nullptr;}
    bool ok=fread(result,1,length,f)==size_t(length);fclose(f);if(!ok){free(result);return nullptr;}
    result[length]=0;if(size)*size=length;return result;
  }
  ZipFile zip(m_path.c_str());
  std::string decoded;
  for(size_t i=0;i<item_href.size();i++){if(item_href[i]=='%'&&i+2<item_href.size()&&isxdigit(item_href[i+1])&&isxdigit(item_href[i+2])){decoded+=char(strtoul(item_href.substr(i+1,2).c_str(),nullptr,16));i+=2;}else decoded+=item_href[i];}
  std::string path = normalise_path(decoded);
  auto content = zip.read_file_to_memory(path.c_str(), size);
  if (!content)
  {
    ESP_LOGE(TAG, "Failed to read item %s", path.c_str());
    return nullptr;
  }
  return content;
}

int Epub::get_spine_items_count()
{
  return m_spine.size();
}

std::string &Epub::get_spine_item(int spine_index)
{
  static std::string empty;
  if (spine_index < 0 || spine_index >= (int)m_spine.size()) return empty;
  return m_spine[spine_index].second;
}

EpubTocEntry &Epub::get_toc_item(int toc_index)
{
  return m_toc[toc_index];
}

int Epub::get_toc_items_count()
{
  return m_toc.size();
}

// work out the section index for a toc index
int Epub::get_spine_index_for_toc_index(int toc_index)
{
  // the toc entry should have an href that matches the spine item
  // so we can find the spine index by looking for the href
  for (int i = 0; i < m_spine.size(); i++)
  {
    if (normalise_path(m_spine[i].second) == normalise_path(m_toc[toc_index].href))
    {
      return i;
    }
  }
  ESP_LOGI(TAG, "Section not found");
  // not found - default to the start of the book
  return -1;
}
