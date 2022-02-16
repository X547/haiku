#include "VideoBufferUtils.h"
#include <Bitmap.h>
#include <Message.h>
#include <Region.h>
#include <OS.h>

#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


status_t VideoBufferFromBitmap(VideoBuffer &buf, BBitmap &bmp)
{
	area_info info;
	CheckRet(bmp.InitCheck());
	CheckRet(bmp.Area());
	CheckRet(get_area_info(bmp.Area(), &info));
	buf.ref = BufferRef {
		.offset  = (addr_t)bmp.Bits() - (addr_t)info.address,
		.size    = (uint64)bmp.BitsLength(),
		.kind    = bufferRefArea,
		.area    = {.area = {.id = bmp.Area()}}
	};
	buf.format = BufferFormat {
		.bytesPerRow = bmp.BytesPerRow(),
		.width       = (int32)bmp.Bounds().Width() + 1,
		.height      = (int32)bmp.Bounds().Height() + 1,
		.colorSpace  = bmp.ColorSpace()
	};
	return B_OK;
}

RasBuf32 RasBufFromFromBitmap(BBitmap *bmp)
{
	if (bmp == NULL) {
		return (RasBuf32) {
			.colors = NULL
		};
	}
	return (RasBuf32) {
		.colors = (uint32*)bmp->Bits(),
		.stride = bmp->BytesPerRow() / 4,
		.width = (int32)bmp->Bounds().Width() + 1,
		.height = (int32)bmp->Bounds().Height() + 1,
	};
}


status_t GetRegion(BMessage& msg, const char* name, BRegion*& region)
{
	if (msg.HasInt32(name)) {
		// NULL mark
		region = NULL;
		return B_OK;
	}
	BRect rect;
	CheckRet(msg.FindRect(name, 0, &rect));
	region->MakeEmpty();
	for (int32 i = 0; msg.FindRect(name, i, &rect) >= B_OK; i++) {
		region->Include(rect);
	}
	return B_OK;
}

status_t SetRegion(BMessage& msg, const char* name, const BRegion* region)
{
	if (region == NULL) {
		// NULL mark
		CheckRet(msg.AddInt32(name, 0));
		return B_OK;
	}
	if (region->CountRects() == 0) {
		CheckRet(msg.AddRect(name, BRect()));
	} else {
		for (int32 i = 0; i < region->CountRects(); i++) {
			CheckRet(msg.AddRect(name, region->RectAt(i)));
		}
	}
	return B_OK;
}
