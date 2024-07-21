#include <stdio.h>
#include <new>

#include <dm2/bus/ACPI.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>

#include "ACPIPrivate.h"
extern "C" {
#include "acpi.h"
}

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


//#define TRACE_ACPI_MODULE
#ifdef TRACE_ACPI_MODULE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define ACPI_DRIVER_MODULE_NAME "bus_managers/acpi/driver/v1"


inline void
free_string(char* str)
{
	free(str);
}


typedef CObjectDeleter<char, void, free_string> CStringDeleter;


class AcpiBusDriver: public DeviceDriver {
public:
	AcpiBusDriver(DeviceNode* node): fNode(node) {}
	virtual ~AcpiBusDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t EnumerateChildDevices(DeviceNode* node, const char* root);

private:
	DeviceNode* fNode;
};


class AcpiDeviceImpl: public BusDriver, public AcpiDevice {
public:
	virtual ~AcpiDeviceImpl() = default;

	// BusDriver
	void Free() final {delete this;}
	status_t InitDriver(DeviceNode* node) final;
	void* QueryInterface(const char* name) final;

	// AcpiDevice
	status_t	InstallNotifyHandler(
							uint32 handlerType, acpi_notify_handler handler,
							void *context) final;
	status_t	RemoveNotifyHandler(
							uint32 handlerType, acpi_notify_handler handler) final;

	status_t	InstallAddressSpaceHandler(
							uint32 spaceId,
							acpi_adr_space_handler handler,
							acpi_adr_space_setup setup,	void *data) final;
	status_t	RemoveAddressSpaceHandler(
							uint32 spaceId,
							acpi_adr_space_handler handler) final;

	uint32		GetObjectType() final;
	status_t	GetObject(const char *path, acpi_object_type **_returnValue) final;
	status_t	WalkNamespace(
							uint32 objectType, uint32 maxDepth,
							acpi_walk_callback descendingCallback,
							acpi_walk_callback ascendingCallback,
							void* context, void** returnValue) final;

	status_t	EvaluateMethod(const char *method,
							acpi_objects *args, acpi_data *returnValue) final;

	status_t	WalkResources(char *method,
							acpi_walk_resources_callback callback, void* context) final;

private:
	friend class AcpiBusDriver;

	DeviceNode* fNode {};
	acpi_handle fHandle {};
	uint32 fType {};
	CStringDeleter fPath;
};


// #pragma mark - AcpiBusDriver

status_t
AcpiBusDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<AcpiBusDriver> driver(new(std::nothrow) AcpiBusDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
AcpiBusDriver::Init()
{
	CHECK_RET(EnumerateChildDevices(fNode, "\\"));

	return B_OK;
}


status_t
AcpiBusDriver::EnumerateChildDevices(DeviceNode* node, const char* root)
{
	char result[255];
	void* counter = NULL;

	TRACE(("acpi_enumerate_child_devices: recursing from %s\n", root));

	while (get_next_entry(ACPI_TYPE_ANY, root, result,
			sizeof(result), &counter) == B_OK) {
		uint32 type = get_object_type(result);
		DeviceNode* deviceNode {};

		switch (type) {
			case ACPI_TYPE_POWER:
			case ACPI_TYPE_PROCESSOR:
			case ACPI_TYPE_THERMAL:
			case ACPI_TYPE_DEVICE: {
#if 0
				char nodeName[5] = {
					(char)((type >>  0) % 0x100),
					(char)((type >>  8) % 0x100),
					(char)((type >> 16) % 0x100),
					(char)((type >> 24) % 0x100),
					0
				};
#endif

				device_attr attrs[16] = {
					// info about device
					{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" }},

					{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = result }},

					// location on ACPI bus
					{ ACPI_DEVICE_PATH_ITEM, B_STRING_TYPE, { .string = result }},

					// info about the device
					{ ACPI_DEVICE_TYPE_ITEM, B_UINT32_TYPE, { .ui32 = type }},

					{ B_DEVICE_FLAGS, B_UINT32_TYPE, { .ui32 = B_FIND_MULTIPLE_CHILDREN }},
					{ NULL }
				};

				uint32 attrCount = 4;
				char* hid = NULL;
				char* cidList[8] = { NULL };
				char* uid = NULL;
				ScopeExit scopeExit([&]() {
					free(hid);
					free(uid);
					for (int i = 0; cidList[i] != NULL; i++)
						free(cidList[i]);
				});
				if (type == ACPI_TYPE_DEVICE) {
					if (get_device_info(result, &hid, (char**)&cidList, 8,
						&uid, NULL) == B_OK) {
						if (hid != NULL) {
							attrs[attrCount].name = ACPI_DEVICE_HID_ITEM;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = hid;
							attrCount++;
						}
						for (int i = 0; cidList[i] != NULL; i++) {
							attrs[attrCount].name = ACPI_DEVICE_CID_ITEM;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = cidList[i];
							attrCount++;
						}
						if (uid != NULL) {
							attrs[attrCount].name = ACPI_DEVICE_UID_ITEM;
							attrs[attrCount].type = B_STRING_TYPE;
							attrs[attrCount].value.string = uid;
							attrCount++;
						}
					}
					uint32 addr;
					if (get_device_addr(result, &addr) == B_OK) {
						attrs[attrCount].name = ACPI_DEVICE_ADDR_ITEM;
						attrs[attrCount].type = B_UINT32_TYPE;
						attrs[attrCount].value.ui32 = addr;
						attrCount++;
					}
				}


				ObjectDeleter<AcpiDeviceImpl> busDriver(new(std::nothrow) AcpiDeviceImpl());
				if (!busDriver.IsSet())
					return B_NO_MEMORY;

				if (AcpiGetHandle(NULL, (ACPI_STRING)result, &busDriver->fHandle) != AE_OK)
					return B_ENTRY_NOT_FOUND;

				busDriver->fPath.SetTo(strdup(result));
				if (!busDriver->fPath.IsSet())
					return B_NO_MEMORY;

				busDriver->fType = type;

				CHECK_RET(node->RegisterNode(fNode, busDriver.Detach(), attrs, &deviceNode));
				DeviceNodePutter deviceNodePutter(deviceNode);

				CHECK_RET(EnumerateChildDevices(deviceNode, result));
				break;
			}
			default:
				CHECK_RET(EnumerateChildDevices(node, result));
				break;
		}

	}

	return B_OK;
}


// #pragma mark - AcpiDeviceImpl

status_t
AcpiDeviceImpl::InitDriver(DeviceNode* node)
{
	fNode = node;

	return B_OK;
}

void*
AcpiDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, AcpiDevice::ifaceName) == 0)
		return static_cast<AcpiDevice*>(this);

	return NULL;
}


status_t
AcpiDeviceImpl::InstallNotifyHandler(
	uint32 handlerType, acpi_notify_handler handler,
	void *context)
{
	return install_notify_handler(fHandle, handlerType, handler, context);
}


status_t
AcpiDeviceImpl::RemoveNotifyHandler(
	uint32 handlerType, acpi_notify_handler handler)
{
	return remove_notify_handler(fHandle, handlerType, handler);
}


status_t
AcpiDeviceImpl::InstallAddressSpaceHandler(
	uint32 spaceId,
	acpi_adr_space_handler handler,
	acpi_adr_space_setup setup,	void *data)
{
	return install_address_space_handler(fHandle, spaceId, handler,
		setup, data);
}


status_t
AcpiDeviceImpl::RemoveAddressSpaceHandler(
	uint32 spaceId,
	acpi_adr_space_handler handler)
{
	return remove_address_space_handler(fHandle, spaceId, handler);
}


uint32
AcpiDeviceImpl::GetObjectType()
{
	return fType;
}


status_t
AcpiDeviceImpl::GetObject(const char *path, acpi_object_type **returnValue)
{
	if (!fPath.IsSet())
		return B_BAD_VALUE;
	if (path) {
		char objname[255];
		snprintf(objname, sizeof(objname), "%s.%s", fPath.Get(), path);
		return get_object(objname, returnValue);
	}
	return get_object(fPath.Get(), returnValue);
}


status_t
AcpiDeviceImpl::WalkNamespace(
	uint32 objectType, uint32 maxDepth,
	acpi_walk_callback descendingCallback,
	acpi_walk_callback ascendingCallback,
	void* context, void** returnValue)
{
	return walk_namespace(fHandle, objectType, maxDepth,
		descendingCallback, ascendingCallback, context, returnValue);
}


status_t
AcpiDeviceImpl::EvaluateMethod(const char *method,
	acpi_objects *args, acpi_data *returnValue)
{
	return evaluate_method(fHandle, method, args, returnValue);
}


status_t
AcpiDeviceImpl::WalkResources(char *method,
	acpi_walk_resources_callback callback, void* context)
{
	return walk_resources(fHandle, method, callback, context);
}


static int32
acpi_driver_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			module_info* module;
			return get_module(B_ACPI_MODULE_NAME, &module);
				// this serializes our module initialization
		}

		case B_MODULE_UNINIT:
			return put_module(B_ACPI_MODULE_NAME);
	}

	return B_BAD_VALUE;
}


driver_module_info gAcpiDriverModule = {
	.info = {
		.name = ACPI_DRIVER_MODULE_NAME,
		.std_ops = acpi_driver_std_ops
	},
	.probe = AcpiBusDriver::Probe
};
