#ifndef _VIDEOCONSUMER_H_
#define _VIDEOCONSUMER_H_

#include <VideoNode.h>
#include "BufferQueue.h"

class BRegion;

class _EXPORT VideoConsumer: public VideoNode
{
private:
	BufferQueue fDisplayQueue;
	ArrayDeleter<BRegion> fDirtyRegions;
	int32 fDisplayBufferId;

	void PresentInt(int32 bufferId);
	status_t PresentedInt(int32 bufferId);

public:
	VideoConsumer(const char* name = NULL);
	virtual ~VideoConsumer();

	virtual void SwapChainChanged(bool isValid);

	uint32 DisplayBufferId();
	VideoBuffer* DisplayBuffer();

	status_t Presented();
	virtual void Present(int32 bufferId, const BRegion* dirty);
	virtual void Present(const BRegion* dirty);

	virtual void MessageReceived(BMessage* msg);
};

#endif	// _VIDEOCONSUMER_H_
