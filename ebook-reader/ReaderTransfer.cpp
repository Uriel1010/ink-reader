#include "ReaderTransfer.h"
void ReaderTransfer::start(bool cardReady){ReaderFiles::start(cardReady);if(!web.start(this))ReaderFiles::stop();}
void ReaderTransfer::poll(){if(active()){web.poll();ReaderFiles::poll();}}
void ReaderTransfer::stop(){web.stop();ReaderFiles::stop();}
#include <Arduino.h>
#include <esp_wifi.h>
void ReaderTransfer::diagnostics() const{wifi_mode_t mode=WIFI_MODE_NULL;esp_wifi_get_mode(&mode);Serial.printf("AP active=%d wifi=%d connected=%d busy=%d ssid=%s\n",active(),int(mode),connected(),busy(),ssid().c_str());}
