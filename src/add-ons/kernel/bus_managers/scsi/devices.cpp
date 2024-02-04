/*
 * Copyright 2004-2006, Haiku, Inc. All RightsReserved.
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

/*
	Device node layer.

	When a SCSI bus is registered, this layer scans for SCSI devices
	and registers a node for each of them. Peripheral drivers are on
	top of these nodes.
*/

#include "scsi_internal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <algorithm>


static status_t
scsi_init_device(ScsiDeviceImpl **outDevice, ScsiBusImpl *bus, uint8 target_id, uint8 target_lun,
	uint8 is_atapi, uint8 manual_autosense, const scsi_res_inquiry *inquiry_data, uint8 path_id);


/** free autosense request of device */

static void
scsi_free_autosense_request(ScsiDeviceImpl *device)
{
	SHOW_FLOW0( 3, "" );

	if (device->auto_sense_request != NULL) {
		device->auto_sense_request->Free();
		device->auto_sense_request = NULL;
	}

	if (device->auto_sense_area > 0) {
		delete_area(device->auto_sense_area);
		device->auto_sense_area = 0;
	}
}


/** free all data of device */

void
ScsiDeviceImpl::Free()
{
	ScsiDeviceImpl *device = this;

	SHOW_FLOW0( 3, "" );

	scsi_free_emulation_buffer(device);
	scsi_free_autosense_request(device);

	unregister_kernel_daemon(scsi_dma_buffer_daemon, device);

	scsi_dma_buffer_free(&device->dma_buffer);

	mutex_destroy(&device->dma_buffer_lock);
	delete_sem(device->dma_buffer_owner);

	delete device;
}


/**	copy string src without trailing zero to dst and remove trailing
 *	spaces size of dst is dst_size, size of src is dst_size-1
 */

static void
beautify_string(char *dst, char *src, int dst_size)
{
	int i;

	memcpy(dst, src, dst_size - 1);

	for (i = dst_size - 2; i >= 0; --i) {
		if (dst[i] != ' ')
			break;
	}

	dst[i + 1] = 0;
}


/** register new device */

status_t
scsi_register_device(ScsiBusImpl *bus, uchar target_id,
	uchar target_lun, scsi_res_inquiry *inquiry_data)
{
	bool is_atapi, manual_autosense;
	uint32 orig_max_blocks, max_blocks;

	SHOW_FLOW0( 3, "" );

	// ask for restrictions
	bus->interface->GetRestrictions(
		target_id, &is_atapi, &manual_autosense, &max_blocks);
	if (target_lun != 0)
		dprintf("WARNING: SCSI target %d lun %d getting restrictions without lun\n",
			target_id, target_lun);

	// find maximum transfer blocks
	// set default value to max (need something like ULONG_MAX here)
	orig_max_blocks = ~0;
	bus->node->FindAttrUint32(B_DMA_MAX_TRANSFER_BLOCKS, &orig_max_blocks,
		true);

	max_blocks = std::min(max_blocks, orig_max_blocks);

	{
		char vendor_ident[sizeof( inquiry_data->vendor_ident ) + 1];
		char product_ident[sizeof( inquiry_data->product_ident ) + 1];
		char product_rev[sizeof( inquiry_data->product_rev ) + 1];
		device_attr attrs[] = {
			// connection
			{ SCSI_DEVICE_TARGET_ID_ITEM, B_UINT8_TYPE, { .ui8 = target_id }},
			{ SCSI_DEVICE_TARGET_LUN_ITEM, B_UINT8_TYPE, { .ui8 = target_lun }},

			// inquiry data (used for both identification and information)
			{ SCSI_DEVICE_INQUIRY_ITEM, B_RAW_TYPE,
				{ .raw = { inquiry_data, sizeof( *inquiry_data ) }}},

			// some more info for driver loading
			{ SCSI_DEVICE_TYPE_ITEM, B_UINT8_TYPE, { .ui8 = inquiry_data->device_type }},
			{ SCSI_DEVICE_VENDOR_ITEM, B_STRING_TYPE, { .string = vendor_ident }},
			{ SCSI_DEVICE_PRODUCT_ITEM, B_STRING_TYPE, { .string = product_ident }},
			{ SCSI_DEVICE_REVISION_ITEM, B_STRING_TYPE, { .string = product_rev }},

			// description of peripheral drivers
			{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "scsi" }},

			// extra restriction of maximum number of blocks per transfer
			{ B_DMA_MAX_TRANSFER_BLOCKS, B_UINT32_TYPE, { .ui32 = max_blocks }},

			// atapi emulation
			{ SCSI_DEVICE_IS_ATAPI_ITEM, B_UINT8_TYPE, { .ui8 = is_atapi }},
			// manual autosense
			{ SCSI_DEVICE_MANUAL_AUTOSENSE_ITEM, B_UINT8_TYPE, { .ui8 = manual_autosense }},
			{ NULL }
		};

		beautify_string(vendor_ident, inquiry_data->vendor_ident, sizeof(vendor_ident));
		beautify_string(product_ident, inquiry_data->product_ident, sizeof(product_ident));
		beautify_string(product_rev, inquiry_data->product_rev, sizeof(product_rev));

		ScsiDeviceImpl *device = NULL;
		CHECK_RET(scsi_init_device(&device, bus, target_id, target_lun, is_atapi, manual_autosense,
			inquiry_data, (uint8)-1));

		BusDriver *busDriver = static_cast<BusDriver*>(device);

		return bus->node->RegisterNode(bus->node, busDriver, attrs, NULL);
	}

	return B_OK;
}


// create data structure for a device
static ScsiDeviceImpl *
scsi_create_device(ScsiBusImpl *bus, int target_id, int target_lun)
{
	SHOW_FLOW0( 3, "" );

	ScsiDeviceImpl *device = new(std::nothrow) ScsiDeviceImpl();
	if (device == NULL)
		return NULL;

	device->bus = bus;
	device->target_id = target_id;
	device->target_lun = target_lun;
	device->valid = true;

	scsi_dma_buffer_init(&device->dma_buffer);

	mutex_init(&device->dma_buffer_lock, "dma_buffer");

	device->dma_buffer_owner = create_sem(1, "dma_buffer");
	if (device->dma_buffer_owner < 0)
		goto err;

	register_kernel_daemon(scsi_dma_buffer_daemon, device, 5 * 10);

	return device;

err:
	mutex_destroy(&device->dma_buffer_lock);
	delete device;
	return NULL;
}


/**	prepare autosense request.
 *	this cannot be done on demand but during init as we may
 *	have run out of ccbs when we need it
 */

static status_t
scsi_create_autosense_request(ScsiDeviceImpl *device)
{
	ScsiCcbImpl *request;
	unsigned char *buffer;
	scsi_cmd_request_sense *cmd;
	size_t total_size;

	SHOW_FLOW0( 3, "" );

	device->auto_sense_request = request = static_cast<ScsiCcbImpl*>(device->AllocCcb());
	if (device->auto_sense_request == NULL)
		return B_NO_MEMORY;

	total_size = SCSI_MAX_SENSE_SIZE + sizeof(physical_entry);
	total_size = (total_size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	// allocate buffer for space sense data and S/G list
	device->auto_sense_area = create_area("auto_sense", (void**)&buffer,
		B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_32_BIT_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		// TODO: Use B_FULL_LOCK, if addresses >= 4 GB are supported!
	if (device->auto_sense_area < 0)
		goto err;

	request->data = buffer;
	request->data_length = SCSI_MAX_SENSE_SIZE;
	request->sg_list = (physical_entry *)(buffer + SCSI_MAX_SENSE_SIZE);
	request->sg_count = 1;

	get_memory_map(buffer, SCSI_MAX_SENSE_SIZE,
		(physical_entry *)request->sg_list, 1);

	// disable auto-autosense, just in case;
	// make sure no other request overtakes sense request;
	// buffer is/must be DMA safe as we cannot risk trouble with
	// dynamically allocated DMA buffer
	request->flags = SCSI_DIR_IN | SCSI_DIS_AUTOSENSE |
		SCSI_ORDERED_QTAG | SCSI_DMA_SAFE;

	cmd = (scsi_cmd_request_sense *)request->cdb;
	request->cdb_length = sizeof(*cmd);

	memset(cmd, 0, sizeof(*cmd));
	cmd->opcode = SCSI_OP_REQUEST_SENSE;
	cmd->lun = device->target_lun;
	cmd->allocation_length = SCSI_MAX_SENSE_SIZE;

	return B_OK;

err:
	request->Free();
	return B_NO_MEMORY;
}


#define SET_BIT(field, bit) field[(bit) >> 3] |= 1 << ((bit) & 7)

static status_t
scsi_init_device(ScsiDeviceImpl **outDevice, ScsiBusImpl *bus, uint8 target_id, uint8 target_lun,
	uint8 is_atapi, uint8 manual_autosense, const scsi_res_inquiry *inquiry_data, uint8 path_id)
{
	ScsiDeviceImpl *device;
	status_t res;

	SHOW_FLOW0(3, "");

	device = scsi_create_device(bus, target_id, target_lun);
	if (device == NULL)
		return B_NO_MEMORY;

/*
	// never mind if there is no path - it might be an emulated controller
	path_id = (uint8)-1;
*/

	device->inquiry_data = *inquiry_data;

	// save restrictions
	device->is_atapi = is_atapi;
	device->manual_autosense = manual_autosense;

	// size of device queue must be detected by trial and error, so
	// we start with a really high number and see when the device chokes
	device->total_slots = 4096;

	// disable queuing if bus doesn't support it
	if ((bus->inquiry_data.hba_inquiry & SCSI_PI_TAG_ABLE) == 0)
		device->total_slots = 1;

	// if there is no autosense, disable queuing to make sure autosense is
	// not overtaken by other requests
	if (device->manual_autosense)
		device->total_slots = 1;

	device->left_slots = device->total_slots;

	// get autosense request if required
	if (device->manual_autosense) {
		if (scsi_create_autosense_request(device) != B_OK) {
			res = B_NO_MEMORY;
			goto err;
		}
	}

	// if this is an ATAPI device, we need an emulation buffer
	if (scsi_init_emulation_buffer(device, SCSI_ATAPI_BUFFER_SIZE) != B_OK) {
		res = B_NO_MEMORY;
		goto err;
	}

	memset(device->emulation_map, 0, sizeof(device->emulation_map));

	if (device->is_atapi) {
		SET_BIT(device->emulation_map, SCSI_OP_READ_6);
		SET_BIT(device->emulation_map, SCSI_OP_WRITE_6);
		SET_BIT(device->emulation_map, SCSI_OP_MODE_SENSE_6);
		SET_BIT(device->emulation_map, SCSI_OP_MODE_SELECT_6);
		SET_BIT(device->emulation_map, SCSI_OP_INQUIRY);
	}

	*outDevice = device;
	return B_OK;

err:
	device->Free();
	return res;
}


status_t
ScsiDeviceImpl::InitDriver(DeviceNode *node)
{
	ScsiDeviceImpl *device = this;

	device->node = node;

	return B_OK;
}


void*
ScsiDeviceImpl::QueryInterface(const char *name)
{
	if (strcmp(name, ScsiDevice::ifaceName) == 0)
		return static_cast<ScsiDevice*>(this);

	return NULL;
}


void
ScsiDeviceImpl::DeviceRemoved()
{
	ScsiDeviceImpl *device = this;

	SHOW_FLOW0(3, "");

	// this must be atomic as no lock is used
	device->valid = false;
}


/**	get device info; create a temporary one if it's not registered
 *	(used during detection)
 *	on success, scan_lun_lock of bus is hold
 */

status_t
scsi_force_get_device(ScsiBusImpl *bus, uchar target_id,
	uchar target_lun, ScsiDeviceImpl **res_device)
{
	device_attr attrs[] = {
		{ SCSI_DEVICE_TARGET_ID_ITEM, B_UINT8_TYPE, { .ui8 = target_id }},
		{ SCSI_DEVICE_TARGET_LUN_ITEM, B_UINT8_TYPE, { .ui8 = target_lun }},
		{ NULL }
	};
	DeviceNode *node;
	status_t res;
	ScsiDevice *device;

	SHOW_FLOW0(3, "");

	// very important: only one can use a forced device to avoid double detection
	acquire_sem(bus->scan_lun_lock);

	// check whether device registered already
	node = NULL;
	bus->node->GetNextChildNode(attrs, &node);

	SHOW_FLOW(3, "%p", node);

	if (node != NULL) {
		device = node->QueryBusInterface<ScsiDevice>();
		if (device == NULL) {
			node->ReleaseReference();
			node = NULL;
			res = ENOENT;
		}
	} else {
		// device doesn't exist yet - create a temporary one
		device = scsi_create_device(bus, target_id, target_lun);
		if (device == NULL)
			res = B_NO_MEMORY;
		else
			res = B_OK;
	}

	*res_device = static_cast<ScsiDeviceImpl*>(device);

	if (res != B_OK)
		release_sem(bus->scan_lun_lock);

	return res;
}


/**	cleanup device received from scsi_force_get_device
 *	on return, scan_lun_lock of bus is released
 */

void
scsi_put_forced_device(ScsiDeviceImpl *device)
{
	ScsiBusImpl *bus = device->bus;

	SHOW_FLOW0(3, "");

	if (device->node != NULL) {
		device->node->ReleaseReference();
	} else {
		// device is temporary
		device->Free();
	}

	release_sem(bus->scan_lun_lock);
}


uchar
ScsiDeviceImpl::ResetDevice()
{
	ScsiDeviceImpl *device = this;

	SHOW_FLOW0(3, "");

	if (device->node == NULL)
		return SCSI_DEV_NOT_THERE;

	return device->bus->interface->ResetDevice(
		device->target_id, device->target_lun);
}


status_t
ScsiDeviceImpl::Control(uint32 op, void *buffer, size_t length)
{
	ScsiDeviceImpl *device = this;

	SHOW_FLOW0(3, "");
	return device->bus->interface->Control(
		device->target_id, op, buffer, length);
}
