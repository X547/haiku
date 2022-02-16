#include "CompositeConsumer.h"
#include "VideoBufferUtils.h"

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
		fBitmaps.Unset();
		fBase->Invalidate(fSurface->frame);
	}
}

status_t CompositeConsumer::SwapChainRequested(const SwapChainSpec& spec)
{
	printf("CompositeConsumer::SwapChainRequested(%" B_PRIuSIZE ")\n", spec.bufferCnt);

	uint32 bufferCnt = spec.bufferCnt;
	
	fBitmaps.SetTo(new ObjectDeleter<BBitmap>[bufferCnt]);
	for (uint32 i = 0; i < bufferCnt; i++) {
		fBitmaps[i].SetTo(new BBitmap(fSurface->frame.OffsetToCopy(B_ORIGIN), spec.bufferSpecs[i].colorSpace));
	}
	SwapChain swapChain;
	swapChain.size = sizeof(SwapChain);
	swapChain.presentEffect = spec.presentEffect;
	swapChain.bufferCnt = bufferCnt;
	ArrayDeleter<VideoBuffer> buffers(new VideoBuffer[bufferCnt]);
	swapChain.buffers = buffers.Get();
	for (uint32 i = 0; i < bufferCnt; i++) {
		buffers[i].id = i;
		CheckRet(VideoBufferFromBitmap(buffers[i], *fBitmaps[i].Get()));
	}
	SetSwapChain(&swapChain);
	return B_OK;
}

void CompositeConsumer::Present(int32 bufferId, const BRegion* dirty)
{
	if (GetSwapChain().presentEffect == presentEffectCopy) {
		BBitmap *dstBmp = fBitmaps[DisplayBufferId()].Get();
		BBitmap *srcBmp = fBitmaps[bufferId].Get();
		if (dirty != NULL) {
			for (int32 i = 0; i < dirty->CountRects(); i++) {
				BRect rect = dirty->RectAt(i);
				dstBmp->ImportBits(srcBmp, rect.LeftTop(), rect.LeftTop(), rect.Size());
			}
		} else {
			dstBmp->ImportBits(srcBmp);
		}
	}
	fBase->InvalidateSurface(this, dirty);
	Presented();
}

BBitmap* CompositeConsumer::DisplayBitmap()
{
	int32 bufferId = DisplayBufferId();
	if (bufferId < 0)
		return NULL;
	return fBitmaps[bufferId].Get();
}

RasBuf32 CompositeConsumer::DisplayRasBuf()
{
	return RasBufFromFromBitmap(DisplayBitmap());
}
