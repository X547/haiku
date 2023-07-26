#pragma once

#include <module.h>
#include <TypeConstants.h>

class DeviceNode;
class DeviceDriver;
class BusDriver;


/* standard device node attributes */

#define B_DEVICE_PRETTY_NAME		"device/pretty name"		/* string */
#define B_DEVICE_MAPPING			"device/mapping"			/* string */
#define B_DEVICE_BUS				"device/bus"				/* string */
#define B_DEVICE_FIXED_CHILD		"device/fixed child"		/* string */
#define B_DEVICE_FLAGS				"device/flags"				/* uint32 */

#define B_DEVICE_VENDOR_ID			"device/vendor"				/* uint16 */
#define B_DEVICE_ID					"device/id"					/* uint16 */
#define B_DEVICE_TYPE				"device/type"
	/* uint16, PCI base class */
#define B_DEVICE_SUB_TYPE			"device/subtype"
	/* uint16, PCI sub type */
#define B_DEVICE_INTERFACE			"device/interface"
	/* uint16, PCI class API */

#define B_DEVICE_UNIQUE_ID			"device/unique id"			/* string */

/* device flags */
#define B_FIND_CHILD_ON_DEMAND		0x01
#define B_FIND_MULTIPLE_CHILDREN	0x02
#define B_KEEP_DRIVER_LOADED		0x04

/* DMA attributes */
#define B_DMA_LOW_ADDRESS			"dma/low_address"
#define B_DMA_HIGH_ADDRESS			"dma/high_address"
#define B_DMA_ALIGNMENT				"dma/alignment"
#define B_DMA_BOUNDARY				"dma/boundary"
#define B_DMA_MAX_TRANSFER_BLOCKS	"dma/max_transfer_blocks"
#define B_DMA_MAX_SEGMENT_BLOCKS	"dma/max_segment_blocks"
#define B_DMA_MAX_SEGMENT_COUNT		"dma/max_segment_count"


/* attribute of a device node */
typedef struct {
	const char		*name;
	type_code		type;			/* for supported types, see value */
	union {
		uint8		ui8;			/* B_UINT8_TYPE */
		uint16		ui16;			/* B_UINT16_TYPE */
		uint32		ui32;			/* B_UINT32_TYPE */
		uint64		ui64;			/* B_UINT64_TYPE */
		const char	*string;		/* B_STRING_TYPE */
		struct {					/* B_RAW_TYPE */
			const void *data;
			size_t	length;
		} raw;
	} value;
} device_attr;


struct device_manager_info {
	module_info info;

	DeviceNode *(*get_root_node)();
	status_t (*register_node)(DeviceNode* parent, BusDriver* driver, DeviceNode** node);
};


// interface exposed by driver add-on
struct driver_module_info {
	module_info info;

	status_t (*probe)(DeviceNode* node, DeviceDriver** driver);
};


class DeviceNode {
public:
	virtual int32 AcquireReference() = 0;
	virtual int32 ReleaseReference() = 0;

	virtual DeviceNode* GetParent() const = 0;
	virtual status_t GetNextChildNode(const device_attr *attrs, DeviceNode **node) const = 0;
	virtual status_t FindChildNode(const device_attr *attrs, DeviceNode **node) const = 0;

	virtual status_t GetNextAttr(device_attr** attr) const = 0;
	virtual status_t FindAttr(const char* name, type_code type, int32 index, const void** value) const = 0;

	inline status_t FindAttrUint32(const char* name, uint32* outValue) const {return B_ERROR; /* TODO */}
	inline status_t FindAttrString(const char* name, const char** outValue) const {return B_ERROR; /* TODO */}

	virtual void* QueryBusInterface(const char* ifaceName) = 0;
	virtual void* QueryDriverInterface(const char* ifaceName) = 0;

	template<typename Iface>
	inline Iface* QueryBusInterface();

	virtual status_t RegisterNode(BusDriver* driver, DeviceNode** node) = 0;
	virtual status_t UnregisterNode(DeviceNode* node) = 0;

protected:
	~DeviceNode() = default;
};


class DeviceDriver {
public:
	virtual void Free() {}
	virtual void* QueryInterface(const char* name) {return NULL;}
	virtual status_t RegisterChildDevices() {return B_OK;}
	virtual void DeviceRemoved() {}
	virtual status_t Suspend(int32 state) {return ENOSYS;}
	virtual status_t Resume() {return ENOSYS;}

protected:
	~DeviceDriver() = default;
};


// interface provided for each device node publiched by bus driver
class BusDriver {
public:
	virtual void Free() {}
	virtual const device_attr* Attributes() const = 0;
	virtual void* QueryInterface(const char* name) {return NULL;}
	virtual void DriverChanged() {}
	virtual status_t CreateChildNode(DeviceNode** outNode) {return ENOSYS;};

protected:
	~BusDriver() = default;
};


template<typename Iface>
inline Iface*
DeviceNode::QueryBusInterface()
{
	return (Iface*)QueryBusInterface(Iface::ifaceName);
}
