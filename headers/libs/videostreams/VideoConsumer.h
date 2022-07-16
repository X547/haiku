#ifndef _VIDEOCONSUMER_H_
#define _VIDEOCONSUMER_H_

#include <VideoNode.h>
#include "BufferQueue.h"

class VideoBuffer;
class BRegion;

class _EXPORT VideoConsumer: public VideoNode
{
private:
	BufferQueue fDisplayQueue;
	ArrayDeleter<BRegion> fDirtyRegions;
	int32 fDisplayBufferId;
	uint32 fEra;

	void PresentInt(int32 bufferId, uint32 producerEra);

public:
	VideoConsumer(const char* name = NULL);
	virtual ~VideoConsumer();

	void SwapChainChanged(bool isValid) override;

	uint32 Era() {return fEra;}
	uint32 DisplayBufferId();
	VideoBuffer* DisplayBuffer();

	status_t Presented(const PresentedInfo &presentedInfo);

	virtual void Present(int32 bufferId, const BRegion* dirty);

	void MessageReceived(BMessage* msg) override;
};

#endif	// _VIDEOCONSUMER_H_
