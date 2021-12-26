#ifndef _VIDEOPRODUCER_H_
#define _VIDEOPRODUCER_H_

#include <VideoNode.h>
#include "BufferQueue.h"

class BRegion;


class _EXPORT VideoProducer: public VideoNode
{
private:
	BufferQueue fBufferPool;

public:
	VideoProducer(const char* name = NULL);
	virtual ~VideoProducer();

	virtual void SwapChainChanged(bool isValid);

	int32 RenderBufferId();
	int32 AllocBuffer();
	bool FreeBuffer(int32 bufferId);
	VideoBuffer* RenderBuffer();
	status_t Present(int32 bufferId, const BRegion* dirty = NULL);
	status_t Present(const BRegion* dirty = NULL);
	virtual void Presented();

	void MessageReceived(BMessage* msg) override;
};

#endif	// _VIDEOPRODUCER_H_
