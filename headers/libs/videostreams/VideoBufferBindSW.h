#pragma once

#include "VideoBufferBind.h"
#include <Referenceable.h>
#include "AreaCloner.h"

class MappedBuffer;
class MappedArea;
class SwapChain;


class _EXPORT SwapChainBindSW: public SwapChainBind {
public:
	struct BindedBuffer {
		BReference<MappedArea> area;
		AreaDeleter ownedArea;
		uint8* bits;
	};

private:
	ArrayDeleter<BindedBuffer> fBindedBuffers;

public:
	virtual ~SwapChainBindSW() = default;
	void Unset() override;
	status_t ConnectTo(const SwapChain &swapChain) override;
	status_t Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec) override;

	BindedBuffer *Buffers() {return fBindedBuffers.Get();}
};
