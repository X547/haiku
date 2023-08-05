#include "CompatDriverModuleList.h"

#include <new>

#include "DriverRoster.h"


const char*
CompatDriverModuleList::CompatInfo::GetName() const
{
	return fModule->GetName();
}


int32
CompatDriverModuleList::Count()
{
	return fModules.Count();
}


const char*
CompatDriverModuleList::ModuleNameAt(int32 index)
{
	for (CompatInfo *info = fModuleScores.LeftMost(); info != NULL; info = fModuleScores.Next(info)) {
		if (index == 0)
			return info->GetName();

		index--;
	}

	return NULL;
}


void
CompatDriverModuleList::Clear()
{
	fModules.Clear();
}


void
CompatDriverModuleList::Insert(DriverModuleInfo* module, float score)
{
	CompatInfo *oldInfo = fModules.Find(module->GetName());
	if (oldInfo != NULL) {
		if (score > oldInfo->GetScore()) {
			fModuleScores.Remove(oldInfo);
			oldInfo->SetScore(score);
			fModuleScores.Insert(oldInfo);
		}
		return;
	}

	ObjectDeleter<CompatInfo> info(new(std::nothrow) CompatInfo(module, score));
	if (!info.IsSet()) {
		dprintf("[!] CompatDriverModuleList::Insert(): out of memory\n");
		return;
	}

	fModules.Insert(info.Get());
	fModuleScores.Insert(info.Get());
	info.Detach();
}


void
CompatDriverModuleList::Remove(DriverModuleInfo* module)
{
	ObjectDeleter<CompatInfo> info(fModules.Find(module->GetName()));
	if (!info.IsSet())
		return;

	fModules.Remove(info.Get());
	fModuleScores.Remove(info.Get());
}
