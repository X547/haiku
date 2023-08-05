#include "DriverRoster.h"

#include <dm2/bus/FDT.h>
#include <dm2/bus/PCI.h>
#include <dm2/bus/Virtio.h>

#include "CompatInfoData.h"


DriverRoster DriverRoster::sInstance;


static type_code
NormalizeTypeCode(type_code typeCode) {
	switch (typeCode) {
		case B_INT8_TYPE:
			return B_UINT8_TYPE;
		case B_INT16_TYPE:
			return B_UINT16_TYPE;
		case B_INT32_TYPE:
			return B_UINT32_TYPE;
		case B_INT64_TYPE:
			return B_UINT64_TYPE;
		default:
			return typeCode;
	}
}


static bool
MatchAttr(DeviceNode* node, const KMessageField& field)
{
	type_code typeCode = NormalizeTypeCode(field.TypeCode());

	int32 i = 0;
	const void* value;
	size_t valueSize;
	while (node->FindAttr(field.Name(), typeCode, i++, &value, &valueSize) >= B_OK) {
		for (int32 j = 0; j < field.CountElements(); j++) {
			int32 fldValueSize;
			const void* fldValue = field.ElementAt(j, &fldValueSize);
			if (valueSize == (size_t)fldValueSize && memcmp(value, fldValue, valueSize) == 0)
				return true;
		}
	}

	return false;
}


// #pragma mark - DriverAddonInfo

DriverCompatInfo::~DriverCompatInfo()
{
	while (!fChildInfos.IsEmpty()) {
		DriverCompatInfo* childInfo = fChildInfos.RemoveHead();
		delete childInfo;
	}
}


status_t
DriverCompatInfo::Init(DriverAddonInfo* addonInfo, const KMessage& msg)
{
	const char* moduleName;
	if (msg.FindString("module", &moduleName) >= B_OK) {
		CHECK_RET(addonInfo->AddModule(moduleName, fModuleInfo));
	}

	const void* data;
	int32 size;
	if (msg.FindData("score", B_FLOAT_TYPE, &data, &size) >= B_OK && size == sizeof(float))
		fScore = *(float*)data;

	KMessageField field;
	if (msg.FindField("attrs", B_MESSAGE_TYPE, &field) >= B_OK) {
		data = field.ElementAt(0, &size);
		CHECK_RET(fAttrs.SetTo((void*)data, size, 0, KMessage::KMESSAGE_INIT_FROM_BUFFER | KMessage::KMESSAGE_CLONE_BUFFER));
	}

	if (msg.FindField("driver", B_MESSAGE_TYPE, &field) >= B_OK) {
		for (int32 i = 0; i < field.CountElements(); i++) {
			int32 size;
			const void *data = field.ElementAt(i, &size);
			KMessage subMsg;
			subMsg.SetTo((void*)data, size);

			ObjectDeleter<DriverCompatInfo> childInfo(new(std::nothrow) DriverCompatInfo());
			if (!childInfo.IsSet())
				return B_NO_MEMORY;

			childInfo->fParentInfo = this;
			CHECK_RET(childInfo->Init(addonInfo, subMsg));
			fChildInfos.Insert(childInfo.Detach());
		}
	}

	return B_OK;
}


void
DriverCompatInfo::Match(DeviceNodeImpl* node, MatchContext ctx)
{
	if (fModuleInfo != NULL)
		ctx.moduleInfo = fModuleInfo;

	if (fScore >= 0.0f)
		ctx.score = fScore;

	KMessageField field;
	while (fAttrs.GetNextField(&field) >= B_OK) {
		if (!MatchAttr(node, field))
			return;
	}

	if (fChildInfos.IsEmpty()) {
		node->InsertCompatDriverModule(ctx.moduleInfo, ctx.score);
		return;
	}

	for (
		DriverCompatInfo* childInfo = fChildInfos.First();
		childInfo != NULL;
		childInfo = fChildInfos.GetNext(childInfo)
	) {
		childInfo->Match(node, ctx);
	}
}


void
DriverCompatInfo::Match(DeviceNodeImpl* node)
{
	MatchContext ctx;
	Match(node, ctx);
}


// #pragma mark - DriverModuleInfo

status_t
DriverModuleInfo::Init(DriverAddonInfo* addon, const char* name)
{
	fName.SetTo(strdup(name));
	if (!fName.IsSet())
		return B_NO_MEMORY;

	fAddon = addon;
	return B_OK;
}


// #pragma mark - DriverAddonInfo

DriverAddonInfo::~DriverAddonInfo()
{
	for (;;) {
		DriverModuleInfo *module = fModules.LeftMost();
		if (module == NULL)
			break;

		fModules.Remove(module);
		delete module;
	}
}


status_t
DriverAddonInfo::Init(const char* path, const KMessage& msg)
{
	dprintf("DriverAddonInfo::Init(\"%s\")\n", path);

	fPath.SetTo(strdup(path));
	if (!fPath.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fCompatInfo.Init(this, msg));

	return B_OK;
}


status_t
DriverAddonInfo::AddModule(const char* name, DriverModuleInfo*& outModule)
{
	dprintf("DriverAddonInfo::AddModule(\"%s\")\n", name);
	{
		DriverModuleInfo* module = fModules.Find(name);
		if (module != NULL)
			return B_OK;
	}
	ObjectDeleter<DriverModuleInfo> module(new(std::nothrow) DriverModuleInfo());
	if (!module.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(module->Init(this, name));
	fModules.Insert(module.Get());

	outModule = module.Detach();
	return B_OK;
}


// #pragma mark - DriverRoster

status_t
DriverRoster::Init()
{
	for (int32 i = 0; i < kCompatInfoDataLen; i++) {
		const CompatInfoData& data = kCompatInfoData[i];
		KMessage msg;
		msg.SetTo(data.data);
		ObjectDeleter<DriverAddonInfo> driverAddonInfo(new(std::nothrow) DriverAddonInfo());
		if (!driverAddonInfo.IsSet())
			return B_NO_MEMORY;

		if (driverAddonInfo->Init(data.addonPath, msg) < B_OK) {
			dprintf("[!] DriverAddonInfo::Init(\"%s\") failed\n", data.addonPath);
			continue;
		}

		if (RegisterDriverAddon(driverAddonInfo.Detach()) < B_OK) {
			dprintf("[!] RegisterDriverAddon(\"%s\") failed\n", data.addonPath);
		}
	}

	return B_OK;
}


status_t
DriverRoster::RegisterDriverAddon(DriverAddonInfo* driverAddonPtr)
{
	ObjectDeleter<DriverAddonInfo> driverAddon(driverAddonPtr);

	if (fDriverAddons.Find(driverAddonPtr->GetPath()) != NULL)
		return EEXIST;

	fDriverAddons.Insert(driverAddon.Detach());

	for (
		DeviceNodeImpl* node = fDeviceNodes.First();
		node != NULL;
		node = fDeviceNodes.GetNext(node)
	) {
		driverAddonPtr->fCompatInfo.Match(node);
	}

	return B_OK;
}


void
DriverRoster::UnregisterDriverAddon(DriverAddonInfo* driverAddonPtr)
{
	ObjectDeleter<DriverAddonInfo> driverAddon(driverAddonPtr);

	for (
		DeviceNodeImpl* node = fDeviceNodes.First();
		node != NULL;
		node = fDeviceNodes.GetNext(node)
	) {
		for (
			DriverModuleInfo *module = driverAddon->fModules.LeftMost();
			module != NULL;
			module = driverAddon->fModules.Next(module)
		) {
			node->RemoveCompatDriverModule(module);
		}
	}

	fDriverAddons.Remove(driverAddon.Get());
}


void
DriverRoster::RegisterDeviceNode(DeviceNodeImpl* node)
{
	fDeviceNodes.Insert(node);

	for (
		DriverAddonInfo *driverAddon = fDriverAddons.LeftMost();
		driverAddon != NULL;
		driverAddon = fDriverAddons.Next(driverAddon)
	) {
		driverAddon->fCompatInfo.Match(node);
	}
}


void DriverRoster::UnregisterDeviceNode(DeviceNodeImpl* node)
{
	fDeviceNodes.Remove(node);
}

