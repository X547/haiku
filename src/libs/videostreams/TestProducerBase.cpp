#include "TestProducerBase.h"

#include <Application.h>


void TestProducerBase::Produce()
{
	if (!SwapChainValid() /*|| RenderBufferId() < 0*/) return;
	
	if (RenderBufferId() < 0) {
		printf("[!] RenderBufferId() < 0\n");
		BRegion dirty;
		Prepare(dirty);
		return;
	}

	switch (GetSwapChain().presentEffect) {
		case presentEffectSwap: {
			BRegion dirty;
			Prepare(dirty);
			BRegion combinedDirty(dirty);
			if (fValidPrevBufCnt < 2) {
				const VideoBuffer& buf = *RenderBuffer();
				combinedDirty.Set(BRect(0, 0, buf.format.width - 1, buf.format.height - 1));
				fValidPrevBufCnt++;
			} else {
				combinedDirty.Include(&fPrevDirty);
			}
			Restore(combinedDirty);
			fPrevDirty = dirty;
			Present(fValidPrevBufCnt == 1 ? &combinedDirty : &dirty);
			break;
		}
		case presentEffectCopy:
		default: {
			BRegion dirty;
			Prepare(dirty);
			if (fValidPrevBufCnt < 1) {
				const VideoBuffer& buf = *RenderBuffer();
				dirty.Set(BRect(0, 0, buf.format.width - 1, buf.format.height - 1));
				fValidPrevBufCnt++;
			}
			Restore(dirty);
			Present(&dirty);
			break;
		}
	}
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

	fMappedBuffers.Unset();

	if (isValid) {
		printf("  swapChain: \n");
		printf("    size: %" B_PRIuSIZE "\n", GetSwapChain().size);
		printf("    bufferCnt: %" B_PRIu32 "\n", GetSwapChain().bufferCnt);
		printf("    buffers:\n");
		for (uint32 i = 0; i < GetSwapChain().bufferCnt; i++) {
			printf("      %" B_PRIu32 "\n", i);
			printf("        area: %" B_PRId32 "\n", GetSwapChain().buffers[i].ref.area.id);
			printf("        offset: %" B_PRIuSIZE "\n", GetSwapChain().buffers[i].ref.offset);
			printf("        length: %" B_PRIu64 "\n", GetSwapChain().buffers[i].ref.size);
			printf("        bytesPerRow: %" B_PRIu32 "\n", GetSwapChain().buffers[i].format.bytesPerRow);
			printf("        width: %" B_PRIu32 "\n", GetSwapChain().buffers[i].format.width);
			printf("        height: %" B_PRIu32 "\n", GetSwapChain().buffers[i].format.height);
			printf("        colorSpace: %d\n", GetSwapChain().buffers[i].format.colorSpace);
		}

		fMappedBuffers.SetTo(new MappedBuffer[GetSwapChain().bufferCnt]);
		for (uint32 i = 0; i < GetSwapChain().bufferCnt; i++) {
			auto &mappedBuffer = fMappedBuffers[i];
			mappedBuffer.area = AreaCloner::Map(GetSwapChain().buffers[i].ref.area.id);
			if (mappedBuffer.area->GetAddress() == NULL) {
				printf("[!] mappedArea.adr == NULL\n");
				return;
			}
			mappedBuffer.bits = mappedBuffer.area->GetAddress() + GetSwapChain().buffers[i].ref.offset;
		}

		fValidPrevBufCnt = 0;
		
		Produce();
	}
}
