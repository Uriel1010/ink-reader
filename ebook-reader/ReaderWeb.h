#pragma once
#include <string>
#include <atomic>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
class ReaderTransfer;
class ReaderWeb {
public:
 bool start(ReaderTransfer *owner);
 void poll();
 void stop();
 bool connected() const {return stationCount>0;}
 std::string ssid,password;
private:
 struct Request {uint8_t opcode;uint32_t id;std::string body,result;uint16_t status=0;SemaphoreHandle_t done;};
 ReaderTransfer *owner=nullptr;httpd_handle_t server=nullptr;
 SemaphoreHandle_t gate=nullptr;Request *pending=nullptr;bool accepting=false;
 int dns=-1;std::string token;
 uint32_t lastStationCheck=0;int stationCount=0;
 static esp_err_t serve(httpd_req_t *request);
 esp_err_t handle(httpd_req_t *request);
};
