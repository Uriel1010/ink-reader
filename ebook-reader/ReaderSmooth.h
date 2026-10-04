#pragma once
#include <stdint.h>
bool readerSmoothBegin(bool enabled,int width,int height);
bool readerSmoothActive();
void readerSmoothPixel(int x,int y,uint8_t shade);
bool readerSmoothUnchanged(const uint8_t *mono,int rotation);
bool readerSmoothPresent(uint32_t &elapsed);
void readerSmoothResetDisplay();
const uint8_t *readerSmoothFrame();
