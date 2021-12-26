#include "CompositeConsumer.h"


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

status_t CompositeConsumer::SetupSwapChain()
{
	uint32 bufferCnt = 2 /* spec.bufferCnt */;
	
	fBitmaps.SetTo(new ObjectDeleter<BBitmap>[bufferCnt]);
	for (uint32 i = 0; i < bufferCnt; i++) {
		fBitmaps[i].SetTo(new BBitmap(fSurface->frame.OffsetToCopy(B_ORIGIN), B_RGBA32 /* spec.bufferSpecs[i].colorSpace */));
	}
	SwapChain swapChain;
	swapChain.size = sizeof(SwapChain);
	swapChain.presentEffect = presentEffectSwap;
	swapChain.bufferCnt = bufferCnt;
	ArrayDeleter<VideoBuffer> buffers(new VideoBuffer[bufferCnt]);
	swapChain.buffers = buffers.Get();
	for (uint32 i = 0; i < bufferCnt; i++) {
		area_info info;
		get_area_info(fBitmaps[i]->Area(), &info);
		buffers[i].id = i;
		buffers[i].area        = fBitmaps[i]->Area();
		buffers[i].offset      = (addr_t)fBitmaps[i]->Bits() - (addr_t)info.address;
		buffers[i].length      = fBitmaps[i]->BitsLength();
		buffers[i].bytesPerRow = fBitmaps[i]->BytesPerRow();
		buffers[i].width       = fBitmaps[i]->Bounds().Width() + 1;
		buffers[i].height      = fBitmaps[i]->Bounds().Height() + 1;
		buffers[i].colorSpace  = fBitmaps[i]->ColorSpace();
	}
	SetSwapChain(&swapChain);
	return B_OK;
}

status_t CompositeConsumer::SwapChainRequested(const SwapChainSpec& spec)
{
	printf("CompositeConsumer::SwapChainRequested(%" B_PRIuSIZE ")\n", spec.bufferCnt);

	return SetupSwapChain();
}

void CompositeConsumer::Present(const BRegion* dirty)
{
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
	BBitmap* bmp = DisplayBitmap();
	if (bmp == NULL) {
		RasBuf32 rb = {
			.colors = NULL
		};
		return rb;
	}
	RasBuf32 rb = {
		.colors = (uint32*)bmp->Bits(),
		.stride = bmp->BytesPerRow() / 4,
		.width = (int32)bmp->Bounds().Width() + 1,
		.height = (int32)bmp->Bounds().Height() + 1,
	};
	return rb;
}
