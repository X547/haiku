/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
	Part of Open SCSI bus manager

	Bus node layer.

	Whenever a controller driver publishes a new controller, a new SCSI bus
	for public and internal use is registered in turn. After that, this
	bus is told to rescan for devices. For each device, there is a
	device registered for peripheral drivers. (see devices.c)
*/

#include "scsi_internal.h"

#include <string.h>
#include <malloc.h>
#include <new>


// bus service should hurry up a bit - good controllers don't take much time
// but are very happy to be busy; don't make it realtime though as we
// don't really need that but would risk to steel processing power of
// realtime-demanding threads
#define BUS_SERVICE_PRIORITY B_URGENT_DISPLAY_PRIORITY


/**	implementation of service thread:
 *	it handles DPC and pending requests
 */

static void
scsi_do_service(ScsiBusImpl *bus)
{
	while (true) {
		SHOW_FLOW0( 3, "" );

		// handle DPCs first as they are more urgent
		if (scsi_check_exec_dpc(bus))
			continue;

		if (scsi_check_exec_service(bus))
			continue;

		break;
	}
}


/** main loop of service thread */

static int32
scsi_service_threadproc(void *arg)
{
	ScsiBusImpl *bus = (ScsiBusImpl *)arg;
	int32 processed_notifications = 0;

	SHOW_FLOW(3, "bus = %p", bus);

	while (true) {
		// we handle multiple requests in scsi_do_service at once;
		// to save time, we will acquire all notifications that are sent
		// up to now at once.
		// (Sadly, there is no "set semaphore to zero" function, so this
		//  is a poor-man emulation)
		acquire_sem_etc(bus->start_service, processed_notifications + 1, 0, 0);

		SHOW_FLOW0( 3, "1" );

		if (bus->shutting_down)
			break;

		// get number of notifications _before_ servicing to make sure no new
		// notifications are sent after do_service()
		get_sem_count(bus->start_service, &processed_notifications);

		scsi_do_service(bus);
	}

	return 0;
}


static ScsiBusImpl *
scsi_create_bus(DeviceNode *node, uint8 path_id)
{
	ScsiBusImpl *bus;
	int res;

	SHOW_FLOW0(3, "");

	bus = new(std::nothrow) ScsiBusImpl();
	if (bus == NULL)
		return NULL;

	bus->path_id = path_id;

	if (node->FindAttrUint32(SCSI_DEVICE_MAX_TARGET_COUNT, &bus->max_target_count, true) != B_OK)
		bus->max_target_count = MAX_TARGET_ID + 1;
	if (node->FindAttrUint32(SCSI_DEVICE_MAX_LUN_COUNT, &bus->max_lun_count, true) != B_OK)
		bus->max_lun_count = MAX_LUN_ID + 1;

	// our ScsiCcb only has a uchar for target_id
	if (bus->max_target_count > 256)
		bus->max_target_count = 256;
	// our ScsiCcb only has a uchar for target_lun
	if (bus->max_lun_count > 256)
		bus->max_lun_count = 256;

	bus->node = node;
	bus->lock_count = bus->blocked[0] = bus->blocked[1] = 0;
	bus->sim_overflow = 0;
	bus->shutting_down = false;

	bus->waiting_devices = NULL;
	//bus->resubmitted_req = NULL;

	bus->dpc_list = NULL;

	if ((bus->scan_lun_lock = create_sem(1, "scsi_scan_lun_lock")) < 0) {
		res = bus->scan_lun_lock;
		goto err6;
	}

	bus->start_service = create_sem(0, "scsi_start_service");
	if (bus->start_service < 0) {
		res = bus->start_service;
		goto err4;
	}

	mutex_init(&bus->mutex, "scsi_bus_mutex");
	spinlock_irq_init(&bus->dpc_lock);

	res = scsi_init_ccb_alloc(bus);
	if (res < B_OK)
		goto err2;

	bus->service_thread = spawn_kernel_thread(scsi_service_threadproc,
		"scsi_bus_service", BUS_SERVICE_PRIORITY, bus);

	if (bus->service_thread < 0) {
		res = bus->service_thread;
		goto err1;
	}

	resume_thread(bus->service_thread);

	return bus;

err1:
	scsi_uninit_ccb_alloc(bus);
err2:
	mutex_destroy(&bus->mutex);
	delete_sem(bus->start_service);
err4:
	delete_sem(bus->scan_lun_lock);
err6:
	delete bus;
	return NULL;
}


void
ScsiBusImpl::Free()
{
	ScsiBusImpl *bus = this;

	int32 retcode;

	// noone is using this bus now, time to clean it up
	bus->shutting_down = true;
	release_sem(bus->start_service);

	wait_for_thread(bus->service_thread, &retcode);

	delete_sem(bus->start_service);
	mutex_destroy(&bus->mutex);
	delete_sem(bus->scan_lun_lock);

	scsi_uninit_ccb_alloc(bus);

	delete bus;
}


status_t
ScsiBusImpl::Probe(DeviceNode *node, DeviceDriver **outDriver)
{
	uint8 path_id;
	ScsiBusImpl *bus;
	status_t res;

	SHOW_FLOW0( 3, "" );

	if (node->FindAttrUint8(SCSI_BUS_PATH_ID_ITEM, &path_id, false) != B_OK)
		return B_ERROR;

	bus = scsi_create_bus(node, path_id);
	if (bus == NULL)
		return B_NO_MEMORY;

	// extract controller/protocol restrictions from node
	if (node->FindAttrUint32(B_DMA_ALIGNMENT, &bus->dma_params.alignment,
			true) != B_OK)
		bus->dma_params.alignment = 0;
	if (node->FindAttrUint32(B_DMA_MAX_TRANSFER_BLOCKS,
			&bus->dma_params.max_blocks, true) != B_OK)
		bus->dma_params.max_blocks = 0xffffffff;
	if (node->FindAttrUint32(B_DMA_BOUNDARY,
			&bus->dma_params.dma_boundary, true) != B_OK)
		bus->dma_params.dma_boundary = ~0;
	if (node->FindAttrUint32(B_DMA_MAX_SEGMENT_BLOCKS,
			&bus->dma_params.max_sg_block_size, true) != B_OK)
		bus->dma_params.max_sg_block_size = 0xffffffff;
	if (node->FindAttrUint32(B_DMA_MAX_SEGMENT_COUNT,
			&bus->dma_params.max_sg_blocks, true) != B_OK)
		bus->dma_params.max_sg_blocks = ~0;

	// do some sanity check:
	bus->dma_params.max_sg_block_size &= ~bus->dma_params.alignment;

	if (bus->dma_params.alignment > B_PAGE_SIZE) {
		SHOW_ERROR(0, "Alignment (0x%" B_PRIx32 ") must be less then "
			"B_PAGE_SIZE", bus->dma_params.alignment);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.max_sg_block_size < 1) {
		SHOW_ERROR(0, "Max s/g block size (0x%" B_PRIx32 ") is too small",
			bus->dma_params.max_sg_block_size);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.dma_boundary < B_PAGE_SIZE - 1) {
		SHOW_ERROR(0, "DMA boundary (0x%" B_PRIx32 ") must be at least "
			"B_PAGE_SIZE", bus->dma_params.dma_boundary);
		res = B_ERROR;
		goto err;
	}

	if (bus->dma_params.max_blocks < 1 || bus->dma_params.max_sg_blocks < 1) {
		SHOW_ERROR(0, "Max blocks (%" B_PRIu32 ") and max s/g blocks (%"
			B_PRIu32 ") must be at least 1", bus->dma_params.max_blocks,
			bus->dma_params.max_sg_blocks);
		res = B_ERROR;
		goto err;
	}

	// cache inquiry data
	bus->PathInquiry(&bus->inquiry_data);

	// get max. number of commands on bus
	bus->left_slots = bus->inquiry_data.hba_queue_size;
	SHOW_FLOW( 3, "Bus has %d slots", bus->left_slots );

	*outDriver = static_cast<DeviceDriver*>(bus);

	return B_OK;

err:
	bus->Free();
	return res;
}


void*
ScsiBusImpl::QueryInterface(const char *name)
{
	if (strcmp(name, ScsiBusBus::ifaceName) == 0)
		return static_cast<ScsiBusBus*>(this);

	return NULL;
}


uchar
ScsiBusImpl::PathInquiry(scsi_path_inquiry *inquiry_data)
{
	ScsiBusImpl *bus = this;
	SHOW_FLOW(4, "path_id=%d", bus->path_id);
	return bus->interface->PathInquiry(inquiry_data);
}


uchar
ScsiBusImpl::ResetBus()
{
	ScsiBusImpl *bus = this;
	return bus->interface->ResetBus();
}


ScsiBusBus*
ScsiBusImpl::ToBusBus(ScsiBus* bus)
{
	return static_cast<ScsiBusBus*>(static_cast<ScsiBusImpl*>(bus));
}


ScsiBusDevice*
ScsiBusImpl::ToBusDevice(ScsiDevice* device)
{
	return static_cast<ScsiBusDevice*>(static_cast<ScsiDeviceImpl*>(device));
}


static status_t
scsi_bus_module_init(void)
{
	SHOW_FLOW0(4, "");
	return init_temp_sg();
}


static status_t
scsi_bus_module_uninit(void)
{
	SHOW_INFO0(4, "");

	uninit_temp_sg();
	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return scsi_bus_module_init();
		case B_MODULE_UNINIT:
			return scsi_bus_module_uninit();

		default:
			return B_ERROR;
	}
}


driver_module_info scsi_bus_module = {
	.info = {
		.name = SCSI_BUS_MODULE_NAME,
		.std_ops = std_ops
	},
	.probe = ScsiBusImpl::Probe
};
