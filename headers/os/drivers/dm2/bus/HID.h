#pragma once

#include <dm2/device_manager.h>


#define HID_DEVICE_REPORT_DESC		"hid/report desc"		/* raw */
#define HID_DEVICE_MAX_INPUT_SIZE	"hid/max input size"	/* uint16 */
#define HID_DEVICE_MAX_OUTPUT_SIZE	"hid/max output size"	/* uint16 */
#define HID_DEVICE_VENDOR			"hid/vendor"			/* uint16 */
#define HID_DEVICE_PRODUCT			"hid/product"			/* uint16 */
#define HID_DEVICE_VERSION			"hid/version"			/* uint16 */

#define HID_REPORT_TYPE_INPUT	1
#define HID_REPORT_TYPE_OUTPUT	2
#define HID_REPORT_TYPE_FEATURE	3

#define HID_PROTOCOL_BOOT	0
#define HID_PROTOCOL_REPORT	1

#define HID_POWER_ON	0
#define HID_POWER_SLEEP	1


class HidInputCallback {
public:
	virtual void InputAvailable(status_t status, uint8* data, uint32 actualSize) = 0;

protected:
	~HidInputCallback() = default;
};


class HidDevice {
public:
	static inline const char ifaceName[] = "bus_managers/hid/device";

	virtual status_t Reset() = 0;
	virtual status_t RequestRead(uint32 size, uint8* data, HidInputCallback* callback) = 0;
	virtual void CancelRead() = 0;
	virtual status_t Write(uint32 size, const uint8* data) = 0;
	virtual status_t GetReport(uint8 reportType, uint8 reportId, uint32 size, uint8* data) = 0;
	virtual status_t SetReport(uint8 reportType, uint8 reportId, uint32 size, const uint8* data) = 0;
	// idle time in milliseconds
	virtual status_t GetIdle(uint8 reportId, uint16* idle) = 0;
	virtual status_t SetIdle(uint8 reportId, uint16 idle) = 0;
	virtual status_t GetProtocol(uint16* protocol) = 0;
	virtual status_t SetProtocol(uint16 protocol) = 0;
	virtual status_t SetPower(uint8 power) = 0;

protected:
	~HidDevice() = default;
};
