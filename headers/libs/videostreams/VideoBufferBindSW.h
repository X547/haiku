#pragma once

#include <AutoDeleter.h>
#include <Referenceable.h>
#include "AreaCloner.h"
#include "VideoBuffer.h"

class MappedBuffer;
class MappedArea;
class SwapChain;


class _EXPORT SwapChainBindSW {
public:
	struct BindedBuffer {
		BReference<MappedArea> area;
		AreaDeleter ownedArea;
		uint8* bits;
	};

private:
	ArrayDeleter<BindedBuffer> fBindedBuffers;

public:
	void Unset();
	status_t ConnectTo(const SwapChain &swapChain);
	status_t Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec);

	BindedBuffer *Buffers() {return fBindedBuffers.Get();}
};
