#include "CompatDriverModuleList.h"

#include "DriverRoster.h"


int32
CompatDriverModuleList::Count()
{
	return fModules.Count();
}


const char*
CompatDriverModuleList::ModuleNameAt(int32 index)
{
	return fModules[index]->GetName();
}


void
CompatDriverModuleList::Clear()
{
	fModules.MakeEmpty();
}


void
CompatDriverModuleList::Insert(DriverModuleInfo* module, float score)
{
	// TODO: sort by score

	fModules.Add(module);
}


void
CompatDriverModuleList::Remove(DriverModuleInfo* module)
{
	fModules.Remove(module);
}
