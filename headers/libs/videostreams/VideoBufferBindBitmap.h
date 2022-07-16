#pragma once

#include <AutoDeleter.h>
#include <Referenceable.h>
#include <Bitmap.h>
#include "AreaCloner.h"

class MappedArea;
class SwapChain;
class SwapChainSpec;


class _EXPORT SwapChainBindBitmap {
public:
	struct BindedBuffer {
		BReference<MappedArea> area;
		ObjectDeleter<BBitmap> bitmap;
	};

private:
	ArrayDeleter<BindedBuffer> fBindedBuffers;

public:
	void Unset();
	status_t ConnectTo(const SwapChain &swapChain);
	status_t Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec);

	BindedBuffer *Buffers() {return fBindedBuffers.Get();}
};
