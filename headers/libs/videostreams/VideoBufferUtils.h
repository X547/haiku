#pragma once

#include "VideoBuffer.h"
#include "RasBuf.h"

class BBitmap;
class BMessage;
class BRegion;


status_t _EXPORT VideoBufferFromBitmap(VideoBuffer &buf, BBitmap &bmp);
RasBuf32 _EXPORT RasBufFromFromBitmap(BBitmap *bmp);

status_t GetRegion(BMessage& msg, const char* name, BRegion*& region);
status_t SetRegion(BMessage& msg, const char* name, const BRegion* region);

void _EXPORT DumpSwapChain(const SwapChain &swapChain);
