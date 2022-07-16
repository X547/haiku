#include "VideoConsumer.h"
#include "VideoBuffer.h"

#include <Region.h>
#include <stdio.h>


#define CheckRetVoid(err) {status_t _err = (err); if (_err < B_OK) return;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}
static void Assert(bool cond) {if (!cond) abort();}


//#pragma mark - VideoProducerProxy

class VideoProducerProxy {
private:
	BMessenger fLink;

public:
	VideoProducerProxy(const BMessenger& link): fLink(link) {}
	const BMessenger &Link() {return fLink;}

	status_t PresentedInt(int32 bufferId, const VideoConsumer::PresentedInfo &presentedInfo);
};

status_t VideoProducerProxy::PresentedInt(int32 bufferId, const VideoConsumer::PresentedInfo &presentedInfo)
{
	//printf("VideoConsumerProxy::Presented(%" B_PRId32 ", %" B_PRIu32 ")\n", bufferId, era);
	BMessage msg(videoNodePresentedMsg);
	if (bufferId >= 0) msg.AddInt32("recycleId", bufferId);
	msg.AddUInt32("era", presentedInfo.era);
	if (presentedInfo.suboptimal) {
		msg.AddBool("suboptimal", presentedInfo.suboptimal);
		msg.AddInt32("width", presentedInfo.width);
		msg.AddInt32("height", presentedInfo.height);
	}
	return Link().SendMessage(&msg);
}


//#pragma mark - VideoConsumer

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
	//printf("VideoConsumer::PresentInt(%" B_PRId32 ", %" B_PRIu32 ")\n", bufferId, producerEra);

	Assert(fDisplayQueue.Add(bufferId));
	if (fDisplayQueue.Length() == 1) {
		BRegion& dirty = fDirtyRegions[bufferId];
		Present(bufferId, dirty.CountRects() > 0 ? &dirty : NULL);
	}
}

status_t VideoConsumer::Presented(const PresentedInfo &presentedInfo)
{
	if (!IsConnected() || !SwapChainValid())
		return B_NOT_ALLOWED;

	PresentedInfo presentedInfo2 = presentedInfo;
	presentedInfo2.era = fEra;
	switch (GetSwapChain().presentEffect) {
		case presentEffectSwap: {
			VideoProducerProxy(Link()).PresentedInt(fDisplayBufferId, presentedInfo2);
			fDisplayBufferId = fDisplayQueue.Remove();
			break;
		}
		case presentEffectCopy: {
			VideoProducerProxy(Link()).PresentedInt(fDisplayQueue.Remove(), presentedInfo2);
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

void VideoConsumer::Present(int32 bufferId, const BRegion* dirty)
{
}


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
