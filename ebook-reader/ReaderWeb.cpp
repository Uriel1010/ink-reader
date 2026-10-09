#include "ReaderWeb.h"
#include "ReaderTransfer.h"
#include "ReaderRadio.h"
#include "ReaderFeatures.h"
#include "ReaderWebAssets.h"
#include "ReaderPdfAssets.h"
#include "ReaderPdfImport.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <algorithm>
namespace {
std::string randomText(int count){const char *alphabet="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";std::string result;while(count--)result+=alphabet[esp_random()%32];return result;}
std::string header(httpd_req_t *r,const char *name){size_t n=httpd_req_get_hdr_value_len(r,name);if(!n||n>256)return "";std::string value(n+1,'\0');httpd_req_get_hdr_value_str(r,name,&value[0],value.size());value.resize(n);return value;}
esp_err_t error(httpd_req_t *r,const char *status,const char *message){httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_send(r,message,HTTPD_RESP_USE_STRLEN);}
}
bool ReaderWeb::start(ReaderTransfer *reader){
 owner=reader;stationCount=0;lastStationCheck=0;if(!gate)gate=xSemaphoreCreateMutex();if(!gate)return false;
 Preferences p;if(!p.begin("crowap",false))return false;
 password=p.getString("password","").c_str();
 if(password.size()!=12){password=randomText(12);if(p.putString("password",password.c_str())!=12){p.end();return false;}}
 p.end();uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);char name[32];snprintf(name,sizeof(name),"InkReader-%02X%02X",mac[4],mac[5]);ssid=name;token=randomText(32);
 if(!readerRadioStart(true))return false;
 wifi_config_t config{};memcpy(config.ap.ssid,ssid.data(),ssid.size());config.ap.ssid_len=ssid.size();memcpy(config.ap.password,password.data(),password.size());config.ap.channel=6;config.ap.max_connection=4;config.ap.authmode=WIFI_AUTH_WPA2_PSK;config.ap.pmf_cfg.capable=true;
 if(esp_wifi_set_config(WIFI_IF_AP,&config)!=ESP_OK||esp_wifi_start()!=ESP_OK){stop();return false;}
 dns=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(53);address.sin_addr.s_addr=INADDR_ANY;
 if(dns<0||bind(dns,(sockaddr*)&address,sizeof(address))<0){stop();return false;}fcntl(dns,F_SETFL,O_NONBLOCK);
 httpd_config_t http=HTTPD_DEFAULT_CONFIG();http.stack_size=8192;http.max_open_sockets=5;http.lru_purge_enable=true;http.recv_wait_timeout=10;http.send_wait_timeout=10;http.uri_match_fn=httpd_uri_match_wildcard;
 if(httpd_start(&server,&http)!=ESP_OK){stop();return false;}
 accepting=true;
 httpd_uri_t get{};get.uri="/*";get.method=HTTP_GET;get.handler=serve;get.user_ctx=this;
 httpd_uri_t post=get;post.method=HTTP_POST;
 if(httpd_register_uri_handler(server,&get)!=ESP_OK||httpd_register_uri_handler(server,&post)!=ESP_OK){stop();return false;}return true;
}
esp_err_t ReaderWeb::serve(httpd_req_t *r){return static_cast<ReaderWeb*>(r->user_ctx)->handle(r);}
esp_err_t ReaderWeb::handle(httpd_req_t *r){
 std::string host=header(r,"Host"),origin=header(r,"Origin");bool own=host=="192.168.4.1"||host=="192.168.4.1:80";
 if(!own){httpd_resp_set_status(r,"302 Found");httpd_resp_set_hdr(r,"Location","http://192.168.4.1/");return httpd_resp_send(r,"",0);}
 httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; worker-src 'self' blob:; font-src 'self' blob: data:; style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self'; frame-ancestors 'none'");
 if(!origin.empty()&&origin!="http://192.168.4.1")return error(r,"403 Forbidden","{\"error\":\"Origin rejected\"}");
 if(r->method==HTTP_GET){
  if(strcmp(r->uri,"/api/session")==0){httpd_resp_set_type(r,"application/json");std::string text="{\"product\":\"InkReader\",\"api\":1,\"token\":\""+token+"\",\"clock\":"+(READER_ENABLE_CLOCK_SCREENSAVER?"true":"false")+"}";return httpd_resp_send(r,text.data(),text.size());}
  if(!strcmp(r->uri,"/pdf-import.js")){httpd_resp_set_type(r,"text/javascript; charset=utf-8");return httpd_resp_send(r,readerPdfImport,sizeof(readerPdfImport)-1);}
  if(!strncmp(r->uri,"/pdfjs/",7)){
   for(size_t i=0;i<readerPdfAssetCount;i++){const auto &asset=readerPdfAssets[i];if(strcmp(r->uri,asset.path))continue;
    httpd_resp_set_type(r,asset.mime);httpd_resp_set_hdr(r,"Content-Encoding","gzip");httpd_resp_set_hdr(r,"Cache-Control","public, max-age=86400");
    for(size_t offset=0;offset<asset.size;offset+=4096){size_t count=std::min(size_t(4096),asset.size-offset);if(httpd_resp_send_chunk(r,(const char*)asset.data+offset,count)!=ESP_OK)return ESP_FAIL;}
    return httpd_resp_send_chunk(r,nullptr,0);
   }
   return error(r,"404 Not Found","{\"error\":\"PDF resource not bundled\"}");
  }
  httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,readerWebPage,sizeof(readerWebPage)-1);
 }
 if(header(r,"X-Reader-Token")!=token)return error(r,"403 Forbidden","{\"error\":\"Reconnect to the reader\"}");
 if(strcmp(r->uri,"/api/op")!=0||r->content_len>4104)return error(r,"400 Bad Request","{\"error\":\"Invalid request\"}");
 std::string opText=header(r,"X-Reader-Operation"),idText=header(r,"X-Reader-Request");char *end=nullptr;unsigned long op=strtoul(opText.c_str(),&end,10);if(opText.empty()||*end||op<1||op>15)return error(r,"400 Bad Request","{\"error\":\"Invalid operation\"}");
 unsigned long id=strtoul(idText.c_str(),&end,10);if(idText.empty()||*end||!id)return error(r,"400 Bad Request","{\"error\":\"Invalid request ID\"}");
 Request request;request.opcode=op;request.id=id;request.body.resize(r->content_len);size_t offset=0;
 while(offset<request.body.size()){int n=httpd_req_recv(r,&request.body[offset],request.body.size()-offset);if(n<=0)return ESP_FAIL;offset+=n;}
 request.done=xSemaphoreCreateBinary();if(!request.done)return error(r,"503 Service Unavailable","{\"error\":\"Out of memory\"}");
 xSemaphoreTake(gate,portMAX_DELAY);bool allowed=accepting&&!pending;if(allowed)pending=&request;xSemaphoreGive(gate);
 if(!allowed){vSemaphoreDelete(request.done);return error(r,"409 Conflict","{\"error\":\"Reader is leaving transfer mode\"}");}
 xSemaphoreTake(request.done,portMAX_DELAY);vSemaphoreDelete(request.done);
 if(request.status){httpd_resp_set_status(r,request.status==9?"409 Conflict":"400 Bad Request");}
 httpd_resp_set_type(r,op==8&&request.status==0?"application/octet-stream":"application/json");
 return httpd_resp_send(r,request.result.data(),request.result.size());
}
void ReaderWeb::poll(){
 if(!gate)return;
 xSemaphoreTake(gate,portMAX_DELAY);Request *r=pending;pending=nullptr;xSemaphoreGive(gate);
 if(r){r->result=owner->execute(r->opcode,r->id,r->body,r->status);xSemaphoreGive(r->done);}
 if(uint32_t(millis()-lastStationCheck)>=1000){
  lastStationCheck=millis();wifi_sta_list_t stations{};
  if(esp_wifi_ap_get_sta_list(&stations)==ESP_OK){
   bool disconnected=stationCount>0&&stations.num==0;stationCount=stations.num;
   if(disconnected&&owner->busy()){uint16_t status;owner->execute(11,0,"{}",status);}
  }
 }
 // Answer one bounded DNS packet per loop, including only a validated first question.
 if(dns>=0){uint8_t bytes[512];sockaddr_in client{};socklen_t len=sizeof(client);int n=recvfrom(dns,bytes,sizeof(bytes)-16,0,(sockaddr*)&client,&len);
  if(n>=17&&!(bytes[2]&0x80)&&bytes[4]==0&&bytes[5]==1){int cursor=12;bool valid=true;while(cursor<n&&bytes[cursor]){int count=bytes[cursor++];if(count>63||cursor+count>=n){valid=false;break;}cursor+=count;}if(valid&&cursor+5<=n){int qend=cursor+5;uint16_t type=uint16_t(bytes[cursor+1])<<8|bytes[cursor+2];bool answer=type==1&&bytes[cursor+3]==0&&bytes[cursor+4]==1;bytes[2]=0x81;bytes[3]=0x80;bytes[6]=bytes[8]=bytes[9]=bytes[10]=bytes[11]=0;bytes[7]=answer?1:0;const uint8_t a[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,192,168,4,1};if(answer)memcpy(bytes+qend,a,sizeof(a));sendto(dns,bytes,qend+(answer?sizeof(a):0),0,(sockaddr*)&client,len);}}
 }
}
void ReaderWeb::stop(){
 if(gate){xSemaphoreTake(gate,portMAX_DELAY);accepting=false;if(pending){pending->status=3;pending->result="{\"error\":\"Transfer mode ended\"}";xSemaphoreGive(pending->done);pending=nullptr;}xSemaphoreGive(gate);}
 if(server){httpd_stop(server);server=nullptr;}if(dns>=0){close(dns);dns=-1;}readerRadioStop();token.clear();stationCount=0;
}
