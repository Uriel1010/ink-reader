#pragma once
bool imageQualitySelfTest();
#include <stdint.h>
#include <stddef.h>
bool renderBookImage(uint8_t *frame, uint8_t *data, size_t size, int x, int y, int w, int h, int canvasWidth=400, int canvasHeight=300);
