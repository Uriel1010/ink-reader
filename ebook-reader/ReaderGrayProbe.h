#pragma once
#include <stdint.h>
bool readerGrayFramePresent(const uint8_t *pixels,int rotation,uint32_t &elapsed);
// Opt-in four-code diagnostic, separate from the three-tone reading renderer.
bool readerGrayProbePresent(int rotation,uint32_t &elapsed);
