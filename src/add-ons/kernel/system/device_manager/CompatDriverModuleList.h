#pragma once

#include <SupportDefs.h>

#include <util/Vector.h>

class DriverModuleInfo;


class CompatDriverModuleList {
public:
	int32 Count();
	const char* ModuleNameAt(int32 index);

	void Clear();
	void Insert(DriverModuleInfo* module, float score);
	void Remove(DriverModuleInfo* module);

private:
	Vector<DriverModuleInfo*> fModules;
};
