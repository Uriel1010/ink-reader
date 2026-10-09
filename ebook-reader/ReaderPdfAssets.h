#pragma once
#include <stdint.h>
#include <stddef.h>
struct ReaderPdfAsset {const char *path,*mime;const uint8_t *data;size_t size;};
extern const ReaderPdfAsset readerPdfAssets[];
extern const size_t readerPdfAssetCount;
