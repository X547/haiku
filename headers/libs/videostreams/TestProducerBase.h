#ifndef _TESTPRODUCERBASE_H_
#define _TESTPRODUCERBASE_H_

#include <VideoProducer.h>

#include "RasBuf.h"

#include <Region.h>
#include <private/shared/AutoDeleter.h>
#include <private/shared/AutoDeleterOS.h>

#include <stdio.h>
#include <map>


struct MappedBuffer
{
	area_id area;
	uint8* bits;
};

struct MappedArea
{
	AreaDeleter area;
	uint8* adr;

	MappedArea(area_id srcArea):
		area(clone_area("cloned buffer", (void**)&adr, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, srcArea))
	{
		if (!area.IsSet()) {
			printf("can't clone area, assuming kernel area\n");
			area_info info;
			if (get_area_info(srcArea, &info) < B_OK) {
				adr = NULL;
				return;
			}
			adr = (uint8*)info.address;
		}
	}
};


class _EXPORT TestProducerBase: public VideoProducer
{
private:
	ArrayDeleter<MappedBuffer> fMappedBuffers;
	std::map<area_id, MappedArea> fMappedAreas;
	uint32 fValidPrevBufCnt;

	BRegion fPrevDirty;

protected:

	virtual void Prepare(BRegion& dirty) = 0;
	virtual void Restore(const BRegion& dirty) = 0;

	inline RasBuf32 RenderBufferRasBuf();
	void FillRegion(const BRegion& region, uint32 color);

	void Produce();

public:
	TestProducerBase(const char* name);
	virtual ~TestProducerBase();

	void Connected(bool isActive) override;
	void SwapChainChanged(bool isValid) override;
};


RasBuf32 TestProducerBase::RenderBufferRasBuf()
{
	const VideoBuffer& buf = *RenderBuffer();
	RasBuf32 rb = {
		.colors = (uint32*)fMappedBuffers[RenderBufferId()].bits,
		.stride = buf.bytesPerRow / 4,
		.width = buf.width,
		.height = buf.height,		
	};
	return rb;
}


#endif	// _TESTPRODUCERBASE_H_
