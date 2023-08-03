#pragma once

#include <module.h>
#include <TypeConstants.h>

typedef struct IORequest io_request;
struct selectsync;

class DeviceNode;
class DeviceNodeListener;
class DeviceDriver;
class BusDriver;
class DevFsNode;
class DevFsNodeHandle;


/* standard device node attributes */

#define B_DEVICE_PRETTY_NAME		"device/pretty name"		/* string */
#define B_DEVICE_MAPPING			"device/mapping"			/* string */
#define B_DEVICE_BUS				"device/bus"				/* string */
#define B_DEVICE_FIXED_CHILD		"device/fixed child"		/* string */
#define B_DEVICE_FLAGS				"device/flags"				/* uint32 */

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

	// TODO: Maybe not need if node monitor can gracefully handle FS mount.
	status_t (*file_system_mounted)();

	DeviceNode *(*get_root_node)();
};


#define B_DEVICE_MANAGER_MODULE_NAME "system/device_manager/v2"


// interface exposed by driver add-on
struct driver_module_info {
	module_info info;

	status_t (*probe)(DeviceNode* node, DeviceDriver** driver);
};


#define B_DEVICE_MANAGER_DRIVER_MODULE_SUFFIX "driver/v1"


class DeviceNode {
public:
	virtual int32 AcquireReference() = 0;
	virtual int32 ReleaseReference() = 0;

	virtual DeviceNode* GetParent() const = 0;
	virtual status_t GetNextChildNode(const device_attr* attrs, DeviceNode** node) const = 0;
	virtual status_t FindChildNode(const device_attr* attrs, DeviceNode** node) const = 0;

	virtual status_t GetNextAttr(device_attr** attr) const = 0;
	virtual status_t FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const = 0;

	inline status_t FindAttrUint16(const char* name, uint16* outValue, bool recursive = false) const;
	inline status_t FindAttrUint32(const char* name, uint32* outValue, bool recursive = false) const;
	inline status_t FindAttrUint64(const char* name, uint64* outValue, bool recursive = false) const;
	inline status_t FindAttrString(const char* name, const char** outValue, bool recursive = false) const;

	virtual void* QueryBusInterface(const char* ifaceName) = 0;
	virtual void* QueryDriverInterface(const char* ifaceName) = 0;

	template<typename Iface>
	inline Iface* QueryBusInterface();
	template<typename Iface>
	inline Iface* QueryDriverInterface();

	virtual status_t InstallListener(DeviceNodeListener* listener) = 0;
	virtual status_t UninstallListener(DeviceNodeListener* listener) = 0;

	virtual status_t RegisterNode(BusDriver* driver, DeviceNode** node) = 0;
	virtual status_t UnregisterNode(DeviceNode* node) = 0;

	virtual status_t RegisterDevFsNode(const char* path, DevFsNode* driver) = 0;
	virtual status_t UnregisterDevFsNode(const char* path) = 0;

protected:
	~DeviceNode() = default;
};


class DeviceNodeListener {
public:
	virtual void NodeUnregistered() {}
	virtual void DriverAttached() {}
	virtual void DriverDetached() {}

protected:
	~DeviceNodeListener() = default;

	void* fPrivate{};
};


class DeviceDriver {
public:
	virtual void Free() {}
	virtual void* QueryInterface(const char* name) {return NULL;}
	virtual void DeviceRemoved() {}
	virtual status_t Suspend(int32 state) {return ENOSYS;}
	virtual status_t Resume() {return ENOSYS;}

protected:
	~DeviceDriver() = default;
};


// interface provided for each device node published by bus driver
class BusDriver {
public:
	virtual void Free() {}
	// Called by DeviceNode::RegisterNode. DeviceNode::RegisterNode will fail if this method fails.
	virtual status_t InitDriver(DeviceNode* node) {return B_OK;}
	virtual const device_attr* Attributes() const = 0;
	virtual void* QueryInterface(const char* name) {return NULL;}
	virtual void DriverChanged() {}
	virtual status_t CreateChildNode(DeviceNode** outNode) {return ENOSYS;};

protected:
	~BusDriver() = default;
};


class DevFsNode {
public:
	union Capabilities {
		struct {
			uint32 read: 1;
			uint32 write: 1;
			uint32 io: 1;
			uint32 control: 1;
			uint32 select: 1;
			uint32 unused: 27;
		};
		uint32 val;
	};

	virtual void Free() {}
	virtual Capabilities GetCapabilities() const {return {};}
	virtual status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) = 0;

protected:
	~DevFsNode() = default;
};


class DevFsNodeHandle {
public:
	virtual void Free() {}
	virtual status_t Close() {return B_OK;}
	virtual status_t Read(off_t pos, void* buffer, size_t* _length) {return ENOSYS;}
	virtual status_t Write(off_t pos, const void* buffer, size_t* _length) {return ENOSYS;}
	virtual status_t IO(io_request* request) {return ENOSYS;}
	virtual status_t Control(uint32 op, void* buffer, size_t length) {return ENOSYS;}
	virtual status_t Select(uint8 event, selectsync* sync) {return ENOSYS;}
	virtual status_t Deselect(uint8 event, selectsync* sync) {return ENOSYS;}

protected:
	~DevFsNodeHandle() = default;
};


template<typename Iface>
inline Iface*
DeviceNode::QueryBusInterface()
{
	return (Iface*)QueryBusInterface(Iface::ifaceName);
}


template<typename Iface>
inline Iface*
DeviceNode::QueryDriverInterface()
{
	return (Iface*)QueryDriverInterface(Iface::ifaceName);
}


inline status_t
DeviceNode::FindAttrUint16(const char* name, uint16* outValue, bool recursive) const
{
	// TODO: implement recursive
	(void)recursive;

	const void* value {};
	status_t res = FindAttr(name, B_UINT16_TYPE, 0, &value, NULL);
	if (res < B_OK)
		return res;

	*outValue = *(const uint16*)value;
	return res;
}


inline status_t
DeviceNode::FindAttrUint32(const char* name, uint32* outValue, bool recursive) const
{
	// TODO: implement recursive
	(void)recursive;

	const void* value {};
	status_t res = FindAttr(name, B_UINT32_TYPE, 0, &value, NULL);
	if (res < B_OK)
		return res;

	*outValue = *(const uint32*)value;
	return res;
}


inline status_t
DeviceNode::FindAttrUint64(const char* name, uint64* outValue, bool recursive) const
{
	// TODO: implement recursive
	(void)recursive;

	const void* value {};
	status_t res = FindAttr(name, B_UINT64_TYPE, 0, &value, NULL);
	if (res < B_OK)
		return res;

	*outValue = *(const uint64*)value;
	return res;
}


inline status_t
DeviceNode::FindAttrString(const char* name, const char** outValue, bool recursive) const
{
	(void)recursive;

	const void* value {};
	status_t res = FindAttr(name, B_STRING_TYPE, 0, &value, NULL);
	if (res < B_OK)
		return res;

	*outValue = (const char*)value;
	return res;
}
