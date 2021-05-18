#ifndef _VIRTIO_INPUT_DRIVER_H_
#define _VIRTIO_INPUT_DRIVER_H_


#include <SupportDefs.h>
#include <Drivers.h>


enum {
	virtioInputRead     = B_DEVICE_OP_CODES_END + 1,
	virtioInputCancelIO = B_DEVICE_OP_CODES_END + 2,
};


#endif	// _VIRTIO_INPUT_DRIVER_H_
