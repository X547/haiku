#pragma once

#include <AutoDeleter.h>
#include "VideoBuffer.h"


class _EXPORT SwapChainBind {
public:
	virtual ~SwapChainBind();
	virtual void Unset() = 0;
	virtual status_t ConnectTo(const SwapChain &swapChain) = 0;
	virtual status_t Alloc(ObjectDeleter<SwapChain> &swapChain, const SwapChainSpec &spec) = 0;
};
