/*
	Driver for USB Ethernet Control Model devices
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#pragma once

#include <dm2/bus/USB.h>

#include <ContainerOf.h>


//#define TRACE_USB_ECM
#ifdef TRACE_USB_ECM
#	define TRACE(x...) dprintf("usb_ecm: " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("usb_ecm: " x)
#define ERROR(x...)			dprintf("\33[33musb_ecm:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


/* class and subclass codes */
#define USB_INTERFACE_CLASS_CDC			0x02
#define USB_INTERFACE_SUBCLASS_ECM		0x06
#define USB_INTERFACE_CLASS_CDC_DATA	0x0a
#define USB_INTERFACE_SUBCLASS_DATA		0x00

/* communication device descriptor subtypes */
#define FUNCTIONAL_SUBTYPE_UNION		0x06
#define FUNCTIONAL_SUBTYPE_ETHERNET		0x0f

typedef struct ethernet_functional_descriptor_s {
	uint8	functional_descriptor_subtype;
	uint8	mac_address_index;
	uint32	ethernet_statistics;
	uint16	max_segment_size;
	uint16	num_multi_cast_filters;
	uint8	num_wakeup_pattern_filters;
} _PACKED ethernet_functional_descriptor;

/* notification definitions */
#define CDC_NOTIFY_NETWORK_CONNECTION		0x00
#define CDC_NOTIFY_CONNECTION_SPEED_CHANGE	0x2a

typedef struct cdc_notification_s {
	uint8	request_type;
	uint8	notification_code;
	uint16	value;
	uint16	index;
	uint16	data_length;
	uint8	data[0];
} _PACKED cdc_notification;

typedef struct cdc_connection_speed_s {
	uint32	upstream_speed; /* in bits/s */
	uint32	downstream_speed; /* in bits/s */
} _PACKED cdc_connection_speed;


class UsbEcmDriver: public DeviceDriver {
public:
	UsbEcmDriver(DeviceNode* node): fNode(node) {}
	virtual ~UsbEcmDriver();

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void DeviceRemoved() final;

private:
	status_t _Init();

	static void _NotifyCallback(void *cookie, int32 status, void *data, size_t actualLength);

	status_t _SetupDevice();
	status_t _ReadMACAddress();

	class DevFsNode: public ::DevFsNode, public ::DevFsNodeHandle {
	public:
		UsbEcmDriver &Base() {return ContainerOf(*this, &UsbEcmDriver::fDevFsNode);}

		Capabilities GetCapabilities() const final;
		status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

		status_t Close() final;
		status_t Read(off_t pos, void* buffer, size_t* length) final;
		status_t Write(off_t pos, const void* buffer, size_t* length) final;
		status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;
	} fDevFsNode;

private:
	DeviceNode*			fNode;
	UsbDevice*			fDevice {};

	// state tracking
	bool				fOpen {};
	bool				fRemoved {};
	int32				fInsideNotify {};
	uint16				fVendorID {};
	uint16				fProductID {};

	// interface and device infos
	uint8				fControlInterfaceIndex {};
	uint8				fDataInterfaceIndex {};
	uint8				fMACAddressIndex {};
	uint16				fMaxSegmentSize {};

	// pipes for notifications and data io
	UsbPipe*			fNotifyEndpoint {};
	UsbPipe*			fReadEndpoint {};
	UsbPipe*			fWriteEndpoint {};

	uint8 *				fNotifyBuffer {};
	uint32				fNotifyBufferLength {};

	// connection data
	sem_id				fLinkStateChangeSem = -1;
	uint8				fMACAddress[6] {};
	bool				fHasConnection {};
	uint32				fDownstreamSpeed {};
	uint32				fUpstreamSpeed {};
};
