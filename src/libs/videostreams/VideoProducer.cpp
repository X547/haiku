#include "VideoProducer.h"

#include <Region.h>
#include <stdio.h>


#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


VideoProducer::VideoProducer(const char* name):
	VideoNode(name)
{}

VideoProducer::~VideoProducer()
{}


void VideoProducer::SwapChainChanged(bool isValid)
{
	int32 bufferCnt = isValid ? GetSwapChain().bufferCnt : 0;
	fBufferPool.SetMaxLen(bufferCnt);
	for (int32 i = 0; i < bufferCnt; i++) {
		fBufferPool.Add(i);
	}
}


int32 VideoProducer::RenderBufferId()
{
	return fBufferPool.Begin();
}

int32 VideoProducer::AllocBuffer()
{
	return fBufferPool.Remove();
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
	if (!IsConnected() || !SwapChainValid())
		return B_NOT_ALLOWED;

	BMessage msg(videoNodePresentMsg);
	msg.AddInt32("bufferId", bufferId);

	if (dirty != NULL) {
		for (int32 i = 0; i < dirty->CountRects(); i++) {
			msg.AddRect("dirty", dirty->RectAt(i));
		}
	}

	CheckRet(Link().SendMessage(&msg));
	
	return B_OK;
}

status_t VideoProducer::Present(const BRegion* dirty)
{
	if ((RenderBufferId() < 0))
		return B_NOT_ALLOWED;

	CheckRet(Present(RenderBufferId(), dirty));
	AllocBuffer();
	return B_OK;
}

void VideoProducer::Presented()
{}


void VideoProducer::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case videoNodePresentedMsg: {
		int32 recycleId;
		if (msg->FindInt32("recycleId", &recycleId) >= B_OK) {
			fBufferPool.Add(recycleId);
		} else {
			recycleId = -1;
		}
		//printf("VideoProducer::presentedMsg(%" B_PRId32 ")\n", recycleId);
		if (RenderBufferId() >= 0) {
			//printf("VideoProducer::Presented(%" B_PRId32 ")\n", RenderBufferId());
			Presented();
		}
		return;
	}
	}
	VideoNode::MessageReceived(msg);
}
