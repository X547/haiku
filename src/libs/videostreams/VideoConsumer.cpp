#include "VideoConsumer.h"

#include <Region.h>
#include <stdio.h>


#define CheckRetVoid(err) {status_t _err = (err); if (_err < B_OK) return;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


VideoConsumer::VideoConsumer(const char* name):
	VideoNode(name), fDisplayBufferId(-1), fEra(0)
{}

VideoConsumer::~VideoConsumer()
{}


void VideoConsumer::SwapChainChanged(bool isValid)
{
	int32 bufferCnt = isValid ? GetSwapChain().bufferCnt : 0;
	fDisplayQueue.SetMaxLen(bufferCnt);
	fDirtyRegions.SetTo((bufferCnt > 0) ? new BRegion[bufferCnt] : NULL);

	if (!isValid || GetSwapChain().presentEffect == presentEffectSwap) {
		fDisplayBufferId = -1;
	} else {
		fDisplayBufferId = 0;
	}
}


uint32 VideoConsumer::DisplayBufferId()
{
	return fDisplayBufferId;
}

VideoBuffer* VideoConsumer::DisplayBuffer()
{
	if (!SwapChainValid())
		return NULL;
	int32 bufId = DisplayBufferId();
	if (bufId < 0)
		return NULL;
	return &GetSwapChain().buffers[bufId];
}

// #pragma mark - Consumer logic

void VideoConsumer::PresentInt(int32 bufferId, uint32 producerEra)
{
	printf("VideoConsumer::PresentInt(%" B_PRId32 ", %" B_PRIu32 ")\n", bufferId, producerEra);
	fDisplayQueue.Add(bufferId);
	if (fDisplayQueue.Length() == 1) {
		BRegion& dirty = fDirtyRegions[bufferId];
		Present(bufferId, dirty.CountRects() > 0 ? &dirty : NULL);
	}
}

status_t VideoConsumer::Presented()
{
	if (!IsConnected() || !SwapChainValid())
		return B_NOT_ALLOWED;

	switch (GetSwapChain().presentEffect) {
		case presentEffectSwap: {
			PresentedInt(fDisplayBufferId);
			fDisplayBufferId = fDisplayQueue.Remove();
			break;
		}
		case presentEffectCopy: {
			PresentedInt(fDisplayQueue.Remove());
			break;
		}
		default:
			return B_NOT_SUPPORTED;
	}
	fEra++;
	if (fDisplayQueue.Length() > 0) {
		int32 bufferId = fDisplayQueue.Begin();
		BRegion& dirty = fDirtyRegions[bufferId];
		Present(bufferId, dirty.CountRects() > 0 ? &dirty : NULL);
	}
	return B_OK;
}

// #pragma mark -

status_t VideoConsumer::PresentedInt(int32 bufferId)
{
	printf("VideoConsumer::PresentedInt(%" B_PRId32 ", %" B_PRIu32 ")\n", bufferId, fEra);
	BMessage msg(videoNodePresentedMsg);
	if (bufferId >= 0) msg.AddInt32("recycleId", bufferId);
	msg.AddUInt32("era", fEra);
	CheckRet(Link().SendMessage(&msg));
	return B_OK;
}

void VideoConsumer::Present(int32 bufferId, const BRegion* dirty)
{
	Present(dirty);
}

void VideoConsumer::Present(const BRegion* dirty)
{}


void VideoConsumer::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case videoNodePresentMsg: {
		int32 bufferId;
		uint32 producerEra;
		if (msg->FindInt32("bufferId", &bufferId) < B_OK)
			return;

		if (msg->FindUInt32("era", &producerEra) < B_OK)
			producerEra = 0;

		BRegion& dirty = fDirtyRegions[bufferId];
		dirty.MakeEmpty();
		BRect rect;
		for (int32 i = 0; msg->FindRect("dirty", i, &rect) >= B_OK; i++) {
			dirty.Include(rect);
		}
		PresentInt(bufferId, producerEra);
		return;
	}
	}
	VideoNode::MessageReceived(msg);
}
