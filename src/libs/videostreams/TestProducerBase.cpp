#include "TestProducerBase.h"

#include <Application.h>


void TestProducerBase::Produce()
{
	if (!SwapChainValid()) return;
	BRegion dirty;
	Prepare(dirty);
	BRegion combinedDirty(dirty);
	if (fValidPrevBufCnt < 2) {
		const VideoBuffer& buf = *RenderBuffer();
		combinedDirty.Set(BRect(0, 0, buf.width - 1, buf.height - 1));
		fValidPrevBufCnt++;
	} else {
		combinedDirty.Include(&fPrevDirty);
	}
	Restore(combinedDirty);
	fPrevDirty = dirty;
	Present(fValidPrevBufCnt == 1 ? &combinedDirty : &dirty);
}


void TestProducerBase::FillRegion(const BRegion& region, uint32 color)
{
	RasBuf32 rb = RenderBufferRasBuf();
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect rect = region.RectAtInt(i);
		rb.Clip2(rect.left, rect.top, rect.right + 1, rect.bottom + 1).Clear(color);
	}
}


TestProducerBase::TestProducerBase(const char* name):
	VideoProducer(name)
{
}

TestProducerBase::~TestProducerBase()
{
	printf("-TestProducer: "); WriteMessenger(BMessenger(this)); printf("\n");
}

void TestProducerBase::Connected(bool isActive)
{
	if (isActive) {
		printf("TestProducer: connected to ");
		WriteMessenger(Link());
		printf("\n");

		SwapChainSpec spec;
		BufferSpec buffers[2];
		spec.size = sizeof(SwapChainSpec);
		spec.presentEffect = presentEffectSwap;
		spec.bufferCnt = 2;
		spec.bufferSpecs = buffers;
		for (uint32 i = 0; i < spec.bufferCnt; i++) {
			buffers[i].colorSpace = B_RGBA32;
		}
		if (RequestSwapChain(spec) < B_OK) {
			printf("[!] can't request swap chain\n");
			exit(1);
		}
	} else {
		printf("TestProducer: disconnected\n");
		be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	}
}

void TestProducerBase::SwapChainChanged(bool isValid)
{
	VideoProducer::SwapChainChanged(isValid);
	printf("TestProducer::SwapChainChanged(%d)\n", isValid);

	fMappedAreas.clear();
	fMappedBuffers.Unset();

	if (isValid) {
		printf("  swapChain: \n");
		printf("    size: %" B_PRIuSIZE "\n", GetSwapChain().size);
		printf("    bufferCnt: %" B_PRIu32 "\n", GetSwapChain().bufferCnt);
		printf("    buffers:\n");
		for (uint32 i = 0; i < GetSwapChain().bufferCnt; i++) {
			printf("      %" B_PRIu32 "\n", i);
			printf("        area: %" B_PRId32 "\n", GetSwapChain().buffers[i].area);
			printf("        offset: %" B_PRIuSIZE "\n", GetSwapChain().buffers[i].offset);
			printf("        length: %" B_PRIu32 "\n", GetSwapChain().buffers[i].length);
			printf("        bytesPerRow: %" B_PRIu32 "\n", GetSwapChain().buffers[i].bytesPerRow);
			printf("        width: %" B_PRIu32 "\n", GetSwapChain().buffers[i].width);
			printf("        height: %" B_PRIu32 "\n", GetSwapChain().buffers[i].height);
			printf("        colorSpace: %d\n", GetSwapChain().buffers[i].colorSpace);
		}

		fMappedBuffers.SetTo(new MappedBuffer[GetSwapChain().bufferCnt]);
		for (uint32 i = 0; i < GetSwapChain().bufferCnt; i++) {
			auto it = fMappedAreas.find(GetSwapChain().buffers[i].area);
			if (it == fMappedAreas.end())
				it = fMappedAreas.emplace(GetSwapChain().buffers[i].area, GetSwapChain().buffers[i].area).first;

			const MappedArea& mappedArea = (*it).second;
			if (mappedArea.adr == NULL) {
				printf("[!] mappedArea.adr == NULL\n");
				return;
			}
			fMappedBuffers[i].bits = mappedArea.adr + GetSwapChain().buffers[i].offset;
		}
		
		fValidPrevBufCnt = 0;
		
		Produce();
	}
}
