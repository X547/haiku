#pragma once

#include <dm2/device_manager.h>
#include <util/Vector.h>


class DriverRoster {
public:
	struct LookupResult {
		float score;
		const char* module;
	};
	typedef Vector<LookupResult> LookupResultArray;


public:
	static DriverRoster& Instance() {return sInstance;}
	status_t Init();

	void Lookup(DeviceNode* node, LookupResultArray& result);

private:
	static DriverRoster sInstance;
};
