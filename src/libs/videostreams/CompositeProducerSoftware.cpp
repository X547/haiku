#include "CompositeProducerSoftware.h"

#include <Application.h>

#include <stdio.h>

static void Assert(bool cond) {if (!cond) abort();}


static void FillRegion(const RasBuf32 &rb, const BRegion& region, uint32 color)
{
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect rect = region.RectAtInt(i);
		rb.Clip2(rect.left, rect.top, rect.right + 1, rect.bottom + 1).Clear(color);
	}
}

void CompositeProducerSoftware::Connected(bool isActive)
{
	if (isActive) {
		printf("CompositeProducerSoftware: connected to ");
		WriteMessenger(Link());
		printf("\n");

		UpdateSwapChain(0, 0);
	} else {
		printf("CompositeProducerSoftware: disconnected\n");
		be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	}
}

void CompositeProducerSoftware::UpdateSwapChain(int32 width, int32 height)
{
		SwapChainSpec spec {
			.size = sizeof(SwapChainSpec),
			.presentEffect = presentEffectSwap,
			.bufferCnt = 2,
			.kind = bufferRefArea,
			.width = width,
			.height = height,
			.colorSpace = B_RGBA32
		};
		if (RequestSwapChain(spec) < B_OK) {
			printf("[!] can't request swap chain\n");
			exit(1);
		}
		fSwapChainChanging = true;
}

void CompositeProducerSoftware::SwapChainChanged(bool isValid)
{
	VideoProducer::SwapChainChanged(isValid);
	printf("TestProducer::SwapChainChanged(%d)\n", isValid);

	fSwapChainBind.Unset();

	if (!isValid) {
		return;
	}
	DumpSwapChain(GetSwapChain());

	fSwapChainBind.ConnectTo(GetSwapChain());

	fValidPrevBufCnt = 0;

	Assert(fPending == 0);
	fSwapChainChanging = false;
	Produce();
}

void CompositeProducerSoftware::Presented(const PresentedInfo &presentedInfo)
{
	fPending--;
	if (presentedInfo.suboptimal) {
		if (fPending == 0) {
			UpdateSwapChain(presentedInfo.width, presentedInfo.height);
		}
	} else {
		if (fPending == 0 && !fSwapChainChanging && fDirty.CountRects() > 0) {
			Produce();
		}
	}
	CompositeProducer::Presented(presentedInfo);
}


void CompositeProducerSoftware::Clear(int32 bufferId, const BRegion& dirty)
{
	RasBuf32 dst = GetRasBuf(bufferId);
	FillRegion(dst, dirty, 0xffcccccc);
}
