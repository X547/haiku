#include "AreaCloner.h"

#include <stdio.h>


MappedArea::MappedArea(area_id srcArea):
	fSrcArea(srcArea),
	fArea(clone_area("cloned buffer", (void**)&fAdr, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, srcArea))
{
	if (!fArea.IsSet()) {
		printf("can't clone area, assuming kernel area\n");
		area_info info;
		if (get_area_info(srcArea, &info) < B_OK) {
			fAdr = NULL;
			return;
		}
		fAdr = (uint8*)info.address;
	}
}

MappedArea::~MappedArea()
{
	AreaCloner::fMappedAreas.erase(fSrcArea);
}


std::map<area_id, MappedArea*> AreaCloner::fMappedAreas;

BReference<MappedArea> AreaCloner::Map(area_id srcArea)
{
	auto it = fMappedAreas.find(srcArea);
	if (it != fMappedAreas.end()) {
		return BReference<MappedArea>(it->second, false);
	}

	it = fMappedAreas.emplace(srcArea, new MappedArea(srcArea)).first;
	return BReference<MappedArea>(it->second, true);
}
