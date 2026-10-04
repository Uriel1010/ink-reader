#pragma once
#include <string>
#include <vector>
#include <stdint.h>
std::vector<std::string> scanGallery(bool cardReady);
bool galleryImage(const std::string &path,uint8_t *canvas,int canvasWidth,int canvasHeight,int x,int y,int width,int height);
void invalidateReaderCaches();
