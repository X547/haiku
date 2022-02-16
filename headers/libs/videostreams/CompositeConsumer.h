#ifndef _COMPOSITECONSUMER_H_
#define _COMPOSITECONSUMER_H_

#include <Region.h>
#include <Bitmap.h>
#include <private/shared/AutoDeleter.h>

#include <VideoConsumer.h>

#include "RasBuf.h"
#include "CompositeProducer.h"


class _EXPORT CompositeConsumer final: public VideoConsumer
{
private:
	CompositeProducer* fBase;
	Surface* fSurface;
	ArrayDeleter<ObjectDeleter<BBitmap> > fBitmaps;

public:
	CompositeConsumer(const char* name, CompositeProducer* base, Surface* surface);
	virtual ~CompositeConsumer();
	
	inline class CompositeProducer* Base() {return fBase;}
	inline class Surface* GetSurface() {return fSurface;}

	void Connected(bool isActive) final;
	status_t SwapChainRequested(const SwapChainSpec& spec) final;
	virtual void Present(int32 bufferId, const BRegion* dirty) final;
	BBitmap* DisplayBitmap();
	RasBuf32 DisplayRasBuf();
};

#endif	// _COMPOSITECONSUMER_H_
