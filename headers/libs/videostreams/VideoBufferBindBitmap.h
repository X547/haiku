#pragma once

#include "VideoBufferBind.h"
#include <Referenceable.h>
#include <Bitmap.h>
#include "AreaCloner.h"

class MappedArea;
class SwapChain;
class SwapChainSpec;


class _EXPORT SwapChainBindBitmap: public SwapChainBind {
public:
	struct BindedBuffer {
		BReference<MappedArea> area;
		ObjectDeleter<BBitmap> bitmap;
	};

private:
	ArrayDeleter<BindedBuffer> fBindedBuffers;

public:
	~SwapChainBindBitmap() = default;
	void Unset() override;
	status_t ConnectTo(const SwapChain &swapChain) override;
	status_t Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec) override;

	BindedBuffer *Buffers() {return fBindedBuffers.Get();}
};
