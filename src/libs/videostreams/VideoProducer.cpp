#include "VideoProducer.h"
#include "VideoBuffer.h"

#include <Looper.h>
#include <Region.h>
#include <stdio.h>

#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}
static void Assert(bool cond) {if (!cond) abort();}

//#pragma mark - VideoConsumerProxy

class VideoConsumerProxy {
private:
	BMessenger fLink;

public:
	VideoConsumerProxy(const BMessenger& link): fLink(link) {}
	const BMessenger &Link() {return fLink;}

	status_t Present(int32 bufferId, const BRegion* dirty, uint32 era);
};

status_t VideoConsumerProxy::Present(int32 bufferId, const BRegion* dirty, uint32 era)
{
	BMessage msg(videoNodePresentMsg);
	msg.AddInt32("bufferId", bufferId);
	msg.AddUInt32("era", era);

	if (dirty != NULL) {
		for (int32 i = 0; i < dirty->CountRects(); i++) {
			msg.AddRect("dirty", dirty->RectAt(i));
		}
	}

	return Link().SendMessage(&msg);
}


static void DumpBufferPool(BufferQueue &bufferPool)
{
	printf("bufferPool: (");
	for (int32 i = 0; i < bufferPool.Length(); i++) {
		if (i > 0) printf(", ");
		printf("%" B_PRId32, bufferPool.ItemAt(i));
	}
	printf(")\n");
}


//#pragma mark - VideoProducer

VideoProducer::VideoProducer(const char* name):
	VideoNode(name), fEra(0)
{}

VideoProducer::~VideoProducer()
{
	for (;;) {
		ObjectDeleter<AllocBufferWaitItem> item(fAllocBufferWaitList.RemoveHead());
		if (!item.IsSet()) break;
		BMessage reply(B_REPLY);
		reply.AddInt32("bufferId", -1);
		item->message->SendReply(&reply);
	}
}


void VideoProducer::SwapChainChanged(bool isValid)
{
	int32 bufferCnt = isValid ? GetSwapChain().bufferCnt : 0;
	fBufferPool.SetMaxLen(bufferCnt);
	// buffer 0 is fixed present buffer
	for (int32 i = (!isValid || GetSwapChain().presentEffect == presentEffectSwap) ? 0 : 1; i < bufferCnt; i++) {
		fBufferPool.Add(i);
	}
	printf("VideoProducer::SwapChainChanged(%d)\n", isValid);
	DumpBufferPool(fBufferPool);
}


int32 VideoProducer::RenderBufferId()
{
	return fBufferPool.Begin();
}

int32 VideoProducer::AllocBuffer()
{
	return fBufferPool.Remove();
}

int32 VideoProducer::AllocBufferWait()
{
	int32 bufferId = AllocBuffer();
	if (bufferId >= 0) {
		return bufferId;
	}
	UnlockLooper();
	BMessage msg(videoNodeAllocBufferMsg);
	BMessage reply;
	BMessenger(this).SendMessage(&msg, &reply);
	if (reply.FindInt32("bufferId", &bufferId) < B_OK) bufferId = -1;
	LockLooper();
	return bufferId;
}

bool VideoProducer::FreeBuffer(int32 bufferId)
{
	return fBufferPool.Add(bufferId);
}

VideoBuffer* VideoProducer::RenderBuffer()
{
	int32 bufferId = RenderBufferId();
	if (bufferId < 0)
		return NULL;
	return &GetSwapChain().buffers[bufferId];
}


status_t VideoProducer::Present(int32 bufferId, const BRegion* dirty)
{
	printf("VideoProducer::Present(%" B_PRId32 ")\n", bufferId);
	DumpBufferPool(fBufferPool);
	if (!IsConnected() || !SwapChainValid())
		return B_NOT_ALLOWED;

	int32 idx = fBufferPool.FindItem(bufferId);
	if (idx >= 0) {
		fBufferPool.RemoveAt(idx);
	}

	CheckRet(VideoConsumerProxy(Link()).Present(bufferId, dirty, fEra));
	fEra++;
	return B_OK;
}

status_t VideoProducer::Present(const BRegion* dirty)
{
	int32 bufferId = AllocBuffer();
	if (bufferId < 0)
		return B_NOT_ALLOWED;

	CheckRet(Present(bufferId, dirty));
	return B_OK;
}

status_t VideoProducer::PresentedInt(int32 recycleId, const PresentedInfo &presentedInfo)
{
	printf("VideoProducer::PresentedInt(%" B_PRId32 ")\n", recycleId);
	DumpBufferPool(fBufferPool);
	if (recycleId >= 0) {
		Assert(fBufferPool.FindItem(recycleId) < 0);
		Assert(fBufferPool.Add(recycleId));
	}
	while (fBufferPool.Begin() >= 0) {
		ObjectDeleter<AllocBufferWaitItem> item(fAllocBufferWaitList.RemoveHead());
		if (!item.IsSet()) break;
		BMessage reply(B_REPLY);
		reply.AddInt32("bufferId", AllocBuffer());
		item->message->SendReply(&reply);
	}
	Presented(presentedInfo);
	return B_OK;
}

void VideoProducer::Presented(const PresentedInfo &presentedInfo)
{}

status_t VideoProducer::GetConsumerInfo(ConsumerInfo &info)
{
	return ENOSYS;
}

void VideoProducer::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case videoNodePresentedMsg: {
		PresentedInfo presentedInfo{};
		int32 recycleId;
		if (msg->FindInt32("recycleId", &recycleId) < B_OK) recycleId = -1;
		msg->FindUInt32("era", &presentedInfo.era);
		if (msg->FindBool("suboptimal", &presentedInfo.suboptimal) < B_OK) presentedInfo.suboptimal = false;
		if (presentedInfo.suboptimal) {
			if (msg->FindInt32("width", &presentedInfo.width) < B_OK) presentedInfo.width = 0;
			if (msg->FindInt32("height", &presentedInfo.height) < B_OK) presentedInfo.height = 0;
		}
		PresentedInt(recycleId, presentedInfo);
		return;
	}
	case videoNodeAllocBufferMsg: {
		int32 bufferId = AllocBuffer();
		if (bufferId >= 0) {
			BMessage reply(B_REPLY);
			reply.AddInt32("bufferId", bufferId);
			msg->SendReply(&reply);
			return;
		}
		AllocBufferWaitItem *item = new AllocBufferWaitItem();
		item->message.SetTo(Looper()->DetachCurrentMessage());
		fAllocBufferWaitList.Insert(item);
		return;
	}
	}
	VideoNode::MessageReceived(msg);
}
