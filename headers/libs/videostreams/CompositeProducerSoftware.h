#pragma once

#include "CompositeProducer.h"


class _EXPORT CompositeProducerSoftware final: public CompositeProducer {
private:
	SwapChainBindSW fSwapChainBind;

	void UpdateSwapChain(int32 width, int32 height);

public:
	using CompositeProducer::CompositeProducer;
	virtual ~CompositeProducerSoftware() = default;

	void Connected(bool isActive) final;
	void SwapChainChanged(bool isValid) final;
	void Presented(const PresentedInfo &presentedInfo) final;

	void Clear(int32 bufferId, const BRegion& dirty) final;

	inline RasBuf32 GetRasBuf(int32 bufferId);
};


RasBuf32 CompositeProducerSoftware::GetRasBuf(int32 bufferId)
{
	if (bufferId < 0) {
		RasBuf32 rb {};
		return rb;
	}
	const VideoBuffer &buf = GetSwapChain().buffers[bufferId];
	const auto &mappedBuffer = fSwapChainBind.Buffers()[bufferId];
	RasBuf32 rb = {
		.colors = (uint32*)mappedBuffer.bits,
		.stride = buf.format.bytesPerRow / 4,
		.width  = buf.format.width,
		.height = buf.format.height,
	};
	return rb;
}
