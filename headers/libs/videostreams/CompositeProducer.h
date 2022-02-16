#ifndef _COMPOSITEPRODUCER_H_
#define _COMPOSITEPRODUCER_H_

#include "TestProducerBase.h"
#include "RasBuf.h"
#include <MessageRunner.h>

#include <private/kernel/util/DoublyLinkedList.h>

class CompositeConsumer;


enum {
	compositeProducerNewSurfaceMsg        = videoNodeInternalLastMsg + 1,
	compositeProducerDeleteSurfaceMsg     = videoNodeInternalLastMsg + 2,
	compositeProducerGetSurfaceMsg        = videoNodeInternalLastMsg + 3,
	compositeProducerUpdateSurfaceMsg     = videoNodeInternalLastMsg + 4,
	compositeProducerInvalidateSurfaceMsg = videoNodeInternalLastMsg + 5,

	compositeProducerInvalidateMsg        = videoNodeInternalLastMsg + 6,
};


enum {
	surfaceFrame = 0,
	surfaceClipping = 1,
	surfaceDrawMode = 2,
};

struct SurfaceUpdate {
	uint32 valid;
	BRect frame;
	BRegion* clipping;
	drawing_mode drawMode;
};


struct Surface: public DoublyLinkedListLinkImpl<Surface>
{
	BRect frame;
	bool clippingEnabled;
	BRegion clipping;
	drawing_mode drawMode;

	ObjectDeleter<CompositeConsumer> consumer;
};


class _EXPORT CompositeProducer final: public VideoProducer
{
private:
	enum {
		stepMsg = videoNodeLastMsg + 1,
	};

	DoublyLinkedList<Surface> fSurfaces;
	ArrayDeleter<MappedBuffer> fMappedBuffers;
	uint32 fValidPrevBufCnt;
	BRegion fDirty, fPrevDirty;
	bool fUpdateRequested;

	void Restore(const BRegion& dirty);

public:
	CompositeProducer(const char* name);
	virtual ~CompositeProducer();

	void Connected(bool isActive) final;
	void SwapChainChanged(bool isValid) final;
	void Presented() final;
	void MessageReceived(BMessage* msg) final;
	
	inline RasBuf32 RenderBufferRasBuf();
	void FillRegion(const BRegion& region, uint32 color);
	void Produce();

	CompositeConsumer* NewSurface(const char* name, const SurfaceUpdate& update);
	status_t DeleteSurface(CompositeConsumer* cons);
	void GetSurface(CompositeConsumer* cons, SurfaceUpdate& update);
	void UpdateSurface(CompositeConsumer* cons, const SurfaceUpdate& update);
	void InvalidateSurface(CompositeConsumer* cons, const BRegion* dirty);

	void Invalidate(const BRect rect);
	void Invalidate(const BRegion& region);
};


RasBuf32 CompositeProducer::RenderBufferRasBuf()
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


status_t GetRegion(BMessage& msg, const char* name, BRegion*& region);
status_t SetRegion(BMessage& msg, const char* name, const BRegion* region);
status_t GetSurfaceUpdate(BMessage& msg, SurfaceUpdate& update);
status_t SetSurfaceUpdate(BMessage& msg, const SurfaceUpdate& update);


#endif	// _COMPOSITEPRODUCER_H_
