#ifndef _PS2_H_
#define _PS2_H_

#include <device_manager.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef void* ps2_device;

typedef status_t (*ps2_interrupt_handler)(void* cookie);


typedef struct {
	driver_module_info info;

	status_t (*read)(ps2_device cookie, uint8* val);
	status_t (*write)(ps2_device cookie, uint8 val);
	void (*set_interrupt_handler)(ps2_device cookie, ps2_interrupt_handler handler, void* handlerCookie);
} ps2_device_interface;

#define PS2_DEVICE_MODULE_NAME "bus_managers/ps2/device/driver_v1"


#ifdef __cplusplus
}
#endif

#endif
