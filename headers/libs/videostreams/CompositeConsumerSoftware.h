#pragma once

#include <Bitmap.h>
#include <private/shared/AutoDeleter.h>

#include <CompositeConsumer.h>
#include <VideoBufferBindBitmap.h>
#include "RasBuf.h"


class _EXPORT CompositeConsumerSoftware final: public CompositeConsumer {
private:
	SwapChainBindBitmap fSwapChainBind;
	uint32 fBitmapCnt = 0;
	BBitmap *fDisplayBitmap = NULL;
	ObjectDeleter<BBitmap> fDisplayBitmapDeleter;

public:
	using CompositeConsumer::CompositeConsumer;
	virtual ~CompositeConsumerSoftware() = default;

	void Connected(bool isActive) final;
	status_t SwapChainRequested(const SwapChainSpec& spec) final;
	void SwapChainChanged(bool isValid) final;
	virtual void Present(int32 bufferId, const BRegion* dirty) final;
	BBitmap* DisplayBitmap();
	RasBuf32 DisplayRasBuf();

	void Draw(int32 bufferId, const BRegion& dirty) final;
};
