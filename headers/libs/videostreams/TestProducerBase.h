#ifndef _TESTPRODUCERBASE_H_
#define _TESTPRODUCERBASE_H_

#include <VideoProducer.h>

#include "RasBuf.h"
#include "AreaCloner.h"

#include <Region.h>
#include <private/shared/AutoDeleter.h>
#include <private/shared/AutoDeleterOS.h>

#include <stdio.h>
#include <map>

struct MappedBuffer
{
	BReference<MappedArea> area;
	uint8* bits;
};


class _EXPORT TestProducerBase: public VideoProducer
{
private:
	ArrayDeleter<MappedBuffer> fMappedBuffers;
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
		.stride = buf.format.bytesPerRow / 4,
		.width = buf.format.width,
		.height = buf.format.height,		
	};
	return rb;
}


#endif	// _TESTPRODUCERBASE_H_
