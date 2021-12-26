#include "VideoConsumer.h"

#include <Region.h>
#include <stdio.h>


#define CheckRetVoid(err) {status_t _err = (err); if (_err < B_OK) return;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


VideoConsumer::VideoConsumer(const char* name):
	VideoNode(name), fDisplayBufferId(-1)
{}

VideoConsumer::~VideoConsumer()
{}


void VideoConsumer::SwapChainChanged(bool isValid)
{
	int32 bufferCnt = isValid ? GetSwapChain().bufferCnt : 0;
	fDisplayQueue.SetMaxLen(bufferCnt);
	fDirtyRegions.SetTo((bufferCnt > 0) ? new BRegion[bufferCnt] : NULL);
	fDisplayBufferId = -1;
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


void VideoConsumer::PresentInt(int32 bufferId)
{
	fDisplayQueue.Add(bufferId);
	if (fDisplayQueue.Length() == 1) {
		BRegion& dirty = fDirtyRegions[bufferId];
		Present(bufferId, dirty.CountRects() > 0 ? &dirty : NULL);
	}
}

status_t VideoConsumer::PresentedInt(int32 bufferId)
{
	//printf("VideoConsumer::PresentedInt(%" B_PRId32 ")\n", bufferId);
	BMessage msg(videoNodePresentedMsg);
	if (bufferId >= 0) msg.AddInt32("recycleId", bufferId);
	CheckRet(Link().SendMessage(&msg));
	return B_OK;
}

status_t VideoConsumer::Presented()
{
	if (!IsConnected() || !SwapChainValid())
		return B_NOT_ALLOWED;

	PresentedInt(fDisplayBufferId);
	fDisplayBufferId = fDisplayQueue.Remove();
	if (fDisplayQueue.Length() > 0) {
		int32 bufferId = fDisplayQueue.Begin();
		BRegion& dirty = fDirtyRegions[bufferId];
		Present(bufferId, dirty.CountRects() > 0 ? &dirty : NULL);
	}
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
		if (msg->FindInt32("bufferId", &bufferId) < B_OK)
			return;

		BRegion& dirty = fDirtyRegions[bufferId];
		dirty.MakeEmpty();
		BRect rect;
		for (int32 i = 0; msg->FindRect("dirty", i, &rect) >= B_OK; i++) {
			dirty.Include(rect);
		}
		PresentInt(bufferId);
		return;
	}
	}
	VideoNode::MessageReceived(msg);
}
