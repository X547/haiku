#include "CompositeConsumer.h"
#include "VideoBufferUtils.h"
#include <stdio.h>

#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


CompositeConsumer::CompositeConsumer(const char* name, CompositeProducer* base, Surface* surface):
	VideoConsumer(name),
	fBase(base),
	fSurface(surface)
{
	printf("+CompositeConsumer: "); WriteMessenger(BMessenger(this)); printf("\n");
}

CompositeConsumer::~CompositeConsumer()
{
	printf("-CompositeConsumer: "); WriteMessenger(BMessenger(this)); printf("\n");
}

void CompositeConsumer::Connected(bool isActive)
{
	if (isActive) {
		printf("CompositeConsumer: connected to ");
		WriteMessenger(Link());
		printf("\n");
	} else {
		printf("CompositeConsumer: disconnected\n");
		SetSwapChain(NULL);
		fSwapChainBind.Unset();
		fDisplayBitmap = NULL;
		fBase->Invalidate(fSurface->frame);
	}
}

status_t CompositeConsumer::SwapChainRequested(const SwapChainSpec& spec0)
{
	printf("CompositeConsumer::SwapChainRequested(%" B_PRIuSIZE ")\n", spec0.bufferCnt);

	// keep old display bitmap
	if (fDisplayBitmap != NULL) {
		for (uint32 i = 0; i < fBitmapCnt; i++) {
			if (fSwapChainBind.Buffers()[i].bitmap.Get() == fDisplayBitmap) {
				fDisplayBitmapDeleter.SetTo(fSwapChainBind.Buffers()[i].bitmap.Detach());
			}
		}
	}

	SwapChainSpec spec = spec0;
	spec.width = (int32)fSurface->frame.Width() + 1;
	spec.height = (int32)fSurface->frame.Height() + 1;

	fBitmapCnt = spec.bufferCnt;
	ObjectDeleter<SwapChain> swapChain;
	CheckRet(fSwapChainBind.Alloc(swapChain, spec));
	SetSwapChain(swapChain.Get());

	return B_OK;
}

void CompositeConsumer::SwapChainChanged(bool isValid)
{
	VideoConsumer::SwapChainChanged(isValid);
	if (!isValid) {
		fSwapChainBind.Unset();
		return;
	}
	if (!OwnsSwapChain()) {
		DumpSwapChain(GetSwapChain());
		fSwapChainBind.ConnectTo(GetSwapChain());
	}
}


void CompositeConsumer::Present(int32 bufferId, const BRegion* dirty)
{
	fDisplayBitmapDeleter.Unset();
	fDisplayBitmap = fSwapChainBind.Buffers()[bufferId].bitmap.Get();

	switch (GetSwapChain().presentEffect) {
		case presentEffectCopy: {
			BBitmap *dstBmp = fSwapChainBind.Buffers()[DisplayBufferId()].bitmap.Get();
			BBitmap *srcBmp = fSwapChainBind.Buffers()[bufferId].bitmap.Get();
			if (dirty != NULL) {
				for (int32 i = 0; i < dirty->CountRects(); i++) {
					BRect rect = dirty->RectAt(i);
					dstBmp->ImportBits(srcBmp, rect.LeftTop(), rect.LeftTop(), rect.Size());
				}
			} else {
				dstBmp->ImportBits(srcBmp);
			}
			break;
		}
		case presentEffectSwap: {
			break;
		}
	}
	fBase->InvalidateSurface(this, dirty);
	PresentedInfo presentedInfo{};
	Presented(presentedInfo);
}

BBitmap* CompositeConsumer::DisplayBitmap()
{
	return fDisplayBitmap;
}

RasBuf32 CompositeConsumer::DisplayRasBuf()
{
	return RasBufFromFromBitmap(DisplayBitmap());
}
