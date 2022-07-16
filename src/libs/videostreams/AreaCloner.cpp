#include "AreaCloner.h"

#include <stdio.h>
#include <string.h>


MappedArea::MappedArea(area_id srcArea):
	fSrcArea(srcArea)
{
	area_info info;
	status_t res = get_area_info(srcArea, &info);
	if (res < B_OK) {
		fprintf(stderr, "[!] MappedArea: bad source area %d: %#" B_PRIx32 "(%s)\n", srcArea, res, strerror(res));
		fAdr = NULL;
		return;
	}
	fArea.SetTo(clone_area("cloned buffer", (void**)&fAdr, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA, srcArea));
	if (!fArea.IsSet()) {
		fprintf(stderr, "[!] MappedArea: can't clone area %d: %#" B_PRIx32 "(%s), assuming kernel area\n", srcArea, fArea.Get(), strerror(fArea.Get()));
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
