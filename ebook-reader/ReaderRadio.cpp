#include "ReaderRadio.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
namespace { bool ready=false,started=false; }
bool readerNetworkReady(){
  if(ready)return true;
  esp_log_level_set("wifi",ESP_LOG_NONE);esp_log_level_set("wifi_init",ESP_LOG_NONE);
  if(esp_netif_init()!=ESP_OK)return false;
  auto result=esp_event_loop_create_default();if(result!=ESP_OK&&result!=ESP_ERR_INVALID_STATE)return false;
  if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())return false;
  ready=true;return true;
}
bool readerRadioStart(bool ap){
  if(!readerNetworkReady())return false;
  readerRadioStop();wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
  if(esp_wifi_init(&config)!=ESP_OK)return false;
  started=true;
  if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(ap?WIFI_MODE_AP:WIFI_MODE_STA)!=ESP_OK){readerRadioStop();return false;}
  return true;
}
void readerRadioStop(){if(started){esp_wifi_disconnect();esp_wifi_stop();esp_wifi_deinit();started=false;}}
