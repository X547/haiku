#include "VideoBufferBindSW.h"
#include "VideoBuffer.h"
#include <stdio.h>


void SwapChainBindSW::Unset()
{
	fBindedBuffers.Unset();
}

status_t SwapChainBindSW::ConnectTo(const SwapChain &swapChain)
{
	fBindedBuffers.SetTo(new BindedBuffer[swapChain.bufferCnt]);
	for (uint32 i = 0; i < swapChain.bufferCnt; i++) {
		auto &mappedBuffer = fBindedBuffers[i];
		mappedBuffer.area = AreaCloner::Map(swapChain.buffers[i].ref.area.id);
		if (mappedBuffer.area->GetAddress() == NULL) {
			printf("[!] mappedArea.adr == NULL\n");
			abort();
			mappedBuffer.bits = NULL;
		} else {
			mappedBuffer.bits = mappedBuffer.area->GetAddress() + swapChain.buffers[i].ref.offset;
		}
	}
	return B_OK;
}

status_t SwapChainBindSW::Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec)
{
	uint8 *swapChainMem = (uint8*)::operator new(sizeof(SwapChain) + spec.bufferCnt*sizeof(VideoBuffer));
	VideoBuffer *buffers = (VideoBuffer*)(swapChainMem + sizeof(SwapChain));
	swapChain.SetTo((SwapChain*)swapChainMem);
	*swapChain.Get() = {
		.size = sizeof(SwapChain),
		.presentEffect = spec.presentEffect,
		.bufferCnt = spec.bufferCnt,
		.buffers = buffers,
	};
	fBindedBuffers.SetTo(new BindedBuffer[spec.bufferCnt]);
	for (uint32 i = 0; i < spec.bufferCnt; i++) {
		auto &buffer = buffers[i];
		auto &mappedBuffer = fBindedBuffers[i];

		buffer = {
			.id = i,
			.format = {
				.width = spec.width != 0 ? spec.width : 1, // !!!
				.height = spec.height != 0 ? spec.height : 1, // !!!
				.colorSpace = B_RGBA32
			}
		};
		buffer.format.bytesPerRow = 4*buffer.format.width;
		buffer.ref = {
			.size = buffer.format.bytesPerRow*buffer.format.height,
			.kind = bufferRefArea,
		};

		mappedBuffer.ownedArea.SetTo(create_area("VideoStreams buffer", (void**)&mappedBuffer.bits, B_ANY_ADDRESS, buffer.ref.size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA));
		buffer.ref.area.id = mappedBuffer.ownedArea.Get();
	}
	return B_OK;
}
