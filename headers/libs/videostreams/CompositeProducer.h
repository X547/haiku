#ifndef _COMPOSITEPRODUCER_H_
#define _COMPOSITEPRODUCER_H_

#include "VideoProducer.h"
#include "VideoBufferBindSW.h"
#include "RasBuf.h"
#include <Region.h>
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


class _EXPORT CompositeProducer: public VideoProducer {
protected:
	enum {
		stepMsg = videoNodeLastMsg + 1,
	};

	DoublyLinkedList<Surface> fSurfaces;
	uint32 fValidPrevBufCnt = 0;
	BRegion fDirty, fPrevDirty;
	bool fUpdateRequested = false;
	bool fSwapChainChanging = false;
	int32 fPending = 0;

	void Restore(int32 bufferId, const BRegion& dirty);

public:
	CompositeProducer(const char* name);
	virtual ~CompositeProducer();

	void MessageReceived(BMessage* msg) final;

	virtual void Clear(int32 bufferId, const BRegion& dirty) = 0;
	void Produce();

	CompositeConsumer* NewSurface(const char* name, const SurfaceUpdate& update);
	status_t DeleteSurface(CompositeConsumer* cons);
	void GetSurface(CompositeConsumer* cons, SurfaceUpdate& update);
	void UpdateSurface(CompositeConsumer* cons, const SurfaceUpdate& update);
	void InvalidateSurface(CompositeConsumer* cons, const BRegion* dirty);

	void Invalidate(const BRect rect);
	void Invalidate(const BRegion& region);
};


status_t GetRegion(BMessage& msg, const char* name, BRegion*& region);
status_t SetRegion(BMessage& msg, const char* name, const BRegion* region);
status_t GetSurfaceUpdate(BMessage& msg, SurfaceUpdate& update);
status_t SetSurfaceUpdate(BMessage& msg, const SurfaceUpdate& update);


#endif	// _COMPOSITEPRODUCER_H_
