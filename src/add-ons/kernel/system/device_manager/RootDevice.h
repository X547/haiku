#pragma once

#include <dm2/device_manager.h>


class RootDevice: public BusDriver {
public:
	RootDevice(bool isRoot = true): fIsRoot(isRoot) {}
	virtual ~RootDevice() = default;

	// BusDriver
	status_t InitDriver(DeviceNode* node) final;
	void Free() final;
	const device_attr* Attributes() const final;
	status_t CreateChildNode(DeviceNode** outNode) final;

private:
	DeviceNode* fNode {};
	bool fIsRoot;
};
