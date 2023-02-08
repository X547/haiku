#include "CompositeConsumerSoftware.h"
#include "CompositeProducerSoftware.h"
#include "VideoBufferUtils.h"

#include <stdio.h>

#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}


void CompositeConsumerSoftware::Connected(bool isActive)
{
	if (isActive) {
		printf("CompositeConsumerSoftware: connected to ");
		WriteMessenger(Link());
		printf("\n");
	} else {
		printf("CompositeConsumerSoftware: disconnected\n");
		SetSwapChain(NULL);
		fSwapChainBind.Unset();
		fDisplayBitmap = NULL;
		Base()->Invalidate(GetSurface()->frame);
	}
}

status_t CompositeConsumerSoftware::SwapChainRequested(const SwapChainSpec& spec0)
{
	printf("CompositeConsumerSoftware::SwapChainRequested(%" B_PRIuSIZE ")\n", spec0.bufferCnt);

	// keep old display bitmap
	if (fDisplayBitmap != NULL) {
		for (uint32 i = 0; i < fBitmapCnt; i++) {
			if (fSwapChainBind.Buffers()[i].bitmap.Get() == fDisplayBitmap) {
				fDisplayBitmapDeleter.SetTo(fSwapChainBind.Buffers()[i].bitmap.Detach());
			}
		}
	}

	SwapChainSpec spec = spec0;
	spec.width = (int32)GetSurface()->frame.Width() + 1;
	spec.height = (int32)GetSurface()->frame.Height() + 1;

	fBitmapCnt = spec.bufferCnt;
	ObjectDeleter<SwapChain> swapChain;
	CheckRet(fSwapChainBind.Alloc(swapChain, spec));
	SetSwapChain(swapChain.Get());

	return B_OK;
}

void CompositeConsumerSoftware::SwapChainChanged(bool isValid)
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


void CompositeConsumerSoftware::Present(int32 bufferId, const BRegion* dirty)
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
	Base()->InvalidateSurface(this, dirty);
	PresentedInfo presentedInfo{};
	Presented(presentedInfo);
}

BBitmap* CompositeConsumerSoftware::DisplayBitmap()
{
	return fDisplayBitmap;
}

RasBuf32 CompositeConsumerSoftware::DisplayRasBuf()
{
	return RasBufFromFromBitmap(DisplayBitmap());
}

void CompositeConsumerSoftware::Draw(int32 bufferId, const BRegion& dirty)
{
	BRegion clipping;
	if (GetSurface()->clippingEnabled) {
		clipping = GetSurface()->clipping;
		clipping.OffsetBy(GetSurface()->frame.LeftTop());
		clipping.IntersectWith(&dirty);
	} else {
		clipping = dirty;
	}

	RasBuf32 dst = static_cast<CompositeProducerSoftware*>(Base())->GetRasBuf(bufferId);
	RasBuf32 displayRb = DisplayRasBuf();
	if (displayRb.colors == NULL) {
		return;
	}
	switch (GetSurface()->drawMode) {
	case B_OP_COPY: {
		for (int32 i = 0; i < clipping.CountRects(); i++) {
			(RasBufOfs<uint32>(dst).ClipOfs(clipping.RectAt(i)) + GetSurface()->frame.LeftTop()).Blit(displayRb);
		}
		break;
	}
	case B_OP_ALPHA:
		for (int32 i = 0; i < clipping.CountRects(); i++) {
			(RasBufOfs<uint32>(dst).ClipOfs(clipping.RectAt(i)) + GetSurface()->frame.LeftTop()).BlitRgb(displayRb);
		}
		break;
	default:
		;
	}
}
