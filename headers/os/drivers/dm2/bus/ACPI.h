#pragma once


#include <dm2/device_manager.h>
#include <ACPI.h>


#define ACPI_DEVICE_ADDR_ITEM	"acpi/addr"
#define ACPI_DEVICE_CID_ITEM	"acpi/cid"
#define ACPI_DEVICE_HANDLE_ITEM	"acpi/handle"
#define ACPI_DEVICE_HID_ITEM	"acpi/hid"
#define ACPI_DEVICE_PATH_ITEM	"acpi/path"
#define ACPI_DEVICE_TYPE_ITEM	"acpi/type"
#define ACPI_DEVICE_UID_ITEM	"acpi/uid"


class AcpiDevice {
public:
	static inline const char ifaceName[] = "bus_managers/acpi/device";

	/* Notify Handler */
	virtual status_t	InstallNotifyHandler(
							uint32 handlerType, acpi_notify_handler handler,
							void *context) = 0;
	virtual status_t	RemoveNotifyHandler(
							uint32 handlerType, acpi_notify_handler handler) = 0;

	/* Address Space Handler */
	virtual status_t	InstallAddressSpaceHandler(
							uint32 spaceId,
							acpi_adr_space_handler handler,
							acpi_adr_space_setup setup,	void *data) = 0;
	virtual status_t	RemoveAddressSpaceHandler(
							uint32 spaceId,
							acpi_adr_space_handler handler) = 0;

	/* Namespace Access */
	virtual uint32		GetObjectType() = 0;
	virtual status_t	GetObject(const char *path, acpi_object_type **_returnValue) = 0;
	virtual status_t	WalkNamespace(
							uint32 objectType, uint32 maxDepth,
							acpi_walk_callback descendingCallback,
							acpi_walk_callback ascendingCallback,
							void* context, void** returnValue) = 0;

	/* Control method execution and data acquisition */
	virtual status_t	EvaluateMethod(const char *method,
							acpi_objects *args, acpi_data *returnValue) = 0;

	/* Resource Management */
	virtual status_t	WalkResources(char *method,
							acpi_walk_resources_callback callback, void* context) = 0;

protected:
	~AcpiDevice() = default;
};
