#ifndef _VIDEOPRODUCER_H_
#define _VIDEOPRODUCER_H_

#include <VideoNode.h>
#include "BufferQueue.h"
#include <private/kernel/util/DoublyLinkedList.h>

class VideoBuffer;
class BRegion;


class VideoProducerProxyBase {
	virtual status_t PresentedInt(int32 recycleId, const VideoNode::PresentedInfo &presentedInfo) = 0;
};

class _EXPORT VideoProducer: public VideoNode {
private:
	struct AllocBufferWaitItem {
		DoublyLinkedListLink<AllocBufferWaitItem> link;
		ObjectDeleter<BMessage> message;
	};

	BufferQueue fBufferPool;
	uint32 fEra;

	DoublyLinkedList<AllocBufferWaitItem, DoublyLinkedListMemberGetLink<AllocBufferWaitItem, &AllocBufferWaitItem::link>> fAllocBufferWaitList;

	status_t PresentedInt(int32 recycleId, const PresentedInfo &presentedInfo);

public:
	VideoProducer(const char* name = NULL);
	virtual ~VideoProducer();

	void SwapChainChanged(bool isValid) override;

	uint32 Era() {return fEra;}
	int32 RenderBufferId();
	int32 AllocBuffer();
	int32 AllocBufferWait();
	bool FreeBuffer(int32 bufferId);
	VideoBuffer* RenderBuffer();
	status_t Present(int32 bufferId, const BRegion* dirty = NULL);
	status_t Present(const BRegion* dirty = NULL);
	status_t GetConsumerInfo(ConsumerInfo &info);

	virtual void Presented(const PresentedInfo &presentedInfo);

	void MessageReceived(BMessage* msg) override;
};

#endif	// _VIDEOPRODUCER_H_
