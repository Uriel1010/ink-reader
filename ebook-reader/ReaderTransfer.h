#pragma once
#include "ReaderFiles.h"
#include "ReaderWeb.h"
class ReaderTransfer: public ReaderFiles {
public:
 void start(bool cardReady);
 void poll();
 void stop();
 void diagnostics() const;
 bool connected() const {return active()&&web.connected();}
 const std::string &ssid() const {return web.ssid;}
 const std::string &password() const {return web.password;}
private:
 ReaderWeb web;
};
