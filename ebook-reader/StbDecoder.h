#pragma once
#include <stdint.h>
#include <stddef.h>
uint8_t *decodeImageFallback(const uint8_t *data,size_t size,int &width,int &height);
void freeImageFallback(uint8_t *pixels);
