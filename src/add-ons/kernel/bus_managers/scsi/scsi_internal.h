/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SCSI_INTERNAL_H
#define _SCSI_INTERNAL_H

//!	Internal structures/definitions

#include <sys/cdefs.h>

#include <dm2/bus/SCSI.h>
#include <scsi_cmds.h>
#include <locked_pool.h>
#include <dm2/device_manager.h>
#include <lock.h>

#define debug_level_error 4
#define debug_level_info 4
#define debug_level_flow 4

#define DEBUG_MSG_PREFIX "SCSI -- "

#include "wrapper.h"
#include "scsi_lock.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define MAX_PATH_ID 255
#define MAX_TARGET_ID 15
#define MAX_LUN_ID 7


// maximum number of fragments for temporary S/G lists
// for real SCSI controllers, there's no limit to transmission length
// but we need a limit - ATA transmits up to 128K, so we allow that
// (for massive data transmission, peripheral drivers should provide own
// SG list anyway)
// add one extra entry in case data is not page aligned
#define MAX_TEMP_SG_FRAGMENTS (128*1024 / B_PAGE_SIZE + 1)

// maximum number of temporary S/G lists
#define MAX_TEMP_SG_LISTS 32

// delay in µs before DMA buffer is cleaned up
#define SCSI_DMA_BUFFER_CLEANUP_DELAY 10*1000000

// buffer size for emulated SCSI commands that ATAPI cannot handle;
// for MODE SELECT 6, maximum size is 255 + header,
// for MODE SENSE 6, we use MODE SENSE 10 which can return 64 K,
// but as the caller has to live with the 255 + header restriction,
// we hope that this buffer is large enough
#define SCSI_ATAPI_BUFFER_SIZE 512


// name of pnp generator of path ids
#define SCSI_PATHID_GENERATOR "scsi/path_id"
// true, if SCSI device needs ATAPI emulation (ui8)
#define SCSI_DEVICE_IS_ATAPI_ITEM "scsi/is_atapi"
// true, if device requires auto-sense emulation (ui8)
#define SCSI_DEVICE_MANUAL_AUTOSENSE_ITEM "scsi/manual_autosense"

#define SCSI_BUS_MODULE_NAME "bus_managers/scsi/device/v1"
// name of internal scsi_bus_raw device driver
#define SCSI_BUS_RAW_MODULE_NAME "bus_managers/scsi/bus/raw/device_v1"

// info about DPC
class ScsiDpcImpl final: public ScsiBusDpc {
public:
	void Free() final;

public:
	struct ScsiDpcImpl *next {};
	bool registered {};			// true, if already/still in dpc list

	void (*func)( void * ) {};
	void *arg {};
};


class ScsiCcbImpl final: public ScsiCcb, public ScsiBusCcb  {
public:
	// ScsiCcb
	void Free() final;

	// ScsiBusCcb
	void Requeue(bool bus_overflow) final;
	void Resubmit() final;
	void Finished(uint num_requests) final;

public:
	ScsiCcbImpl *next {}, *prev {};

	bool ordered : 1;		// request cannot overtake/be overtaken by others
	bool buffered : 1;		// data is buffered to make it DMA safe
	bool emulated : 1;		// command is executed as part of emulation
};


// controller restrictions (see blkman.h)
typedef struct dma_params {
	uint32 alignment;
	uint32 max_blocks;
	uint32 dma_boundary;
	uint32 max_sg_block_size;
	uint32 max_sg_blocks;
} dma_params;


// SCSI bus
class ScsiBusImpl final: public DeviceDriver, public ScsiBus, public ScsiBusBus {
public:
	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** outDriver);
	virtual void Free();
	virtual void* QueryInterface(const char* name);

	// ScsiBus
	uchar PathInquiry(scsi_path_inquiry* inquiry_data) final;
	uchar ResetBus() final;

	// ScsiBusBus
	virtual ScsiBusBus* ToBusBus(ScsiBus* bus) final;
	virtual ScsiBusDevice* ToBusDevice(ScsiDevice* device) final;

	virtual status_t AllocDpc(ScsiBusDpc** dpc) final;
	virtual status_t ScheduleDpc(ScsiBusDpc* dpc, /*int flags,*/
		void (*func)( void * ), void *arg) final;

	virtual void Block() final;
	virtual void Unblock() final;
	virtual void ContSend() final;

public:
	int lock_count {};				// sum of blocked[0..1] and sim_overflow
	int blocked[2] {};				// depth of nested locks by bus manager (0) and SIM (1)
	int left_slots {};				// left command queuing slots on HBA
	bool sim_overflow {};			// 1, if SIM refused req because of bus queue overflow

	uchar path_id {};				// SCSI path id
	uint32 max_target_count {};		// maximum count of target_ids on the bus
	uint32 max_lun_count {};		// maximum count of lun_ids on the bus

	thread_id service_thread {};	// service thread
	sem_id start_service {};		// released whenever service thread has work to do
	bool shutting_down {};			// set to true to tell service thread to shut down

	struct mutex mutex {};			// used to synchronize changes in queueing and blocking

	sem_id scan_lun_lock {};		// allocated whenever a lun is scanned

	ScsiHostController *interface {};	// SIM interface

	spinlock_irq dpc_lock {};		// synchronizer for dpc list
	ScsiDpcImpl *dpc_list {};	// list of dpcs to execute

	struct ScsiDeviceImpl *waiting_devices {};	// devices ready to receive requests

	locked_pool_cookie ccb_pool {};	// ccb pool (one per bus)

	DeviceNode *node {};		// pnp node of bus

	struct dma_params dma_params {};	// dma restrictions of controller

	scsi_path_inquiry inquiry_data {};	// inquiry data as read on init
};


// DMA buffer
typedef struct dma_buffer {
	area_id area;			// area of DMA buffer
	uchar *address;			// address of DMA buffer
	size_t size;			// size of DMA buffer
	area_id sg_list_area;	// area of S/G list
	physical_entry *sg_list;	// address of S/G list
	uint32 sg_count;			// number of entries in S/G list
	bool inuse;				// true, if in use
	bigtime_t last_use;		// timestamp of last usage

	area_id sg_orig;					// area of S/G list to original data
	physical_entry *sg_list_orig;		// S/G list to original data
	uint32 sg_count_max_orig;			// maximum size (in entries)
	uint32 sg_count_orig;				// current size (in entries)

	uchar *orig_data;					// pointer to original data
	const physical_entry *orig_sg_list;	// original S/G list
	uint32 orig_sg_count;				// size of original S/G list
} dma_buffer;


// SCSI device
class ScsiDeviceImpl final: public BusDriver, public ScsiDevice, public ScsiBusDevice {
public:
	// BusDriver
	void Free() final;
	status_t InitDriver(DeviceNode* node) final;
	void* QueryInterface(const char* name) final;
	void DeviceRemoved(); // !!! never used

	// ScsiDevice
	ScsiCcb* AllocCcb() final;
	void AsyncIo(ScsiCcb* ccb) final;
	void SyncIo(ScsiCcb* ccb) final;
	uchar Abort(ScsiCcb* ccb_to_abort) final;
	uchar ResetDevice() final;
	uchar TermIo(ScsiCcb* ccb_to_terminate) final;
	status_t Control(uint32 op, void* buffer, size_t length) final;

	// ScsiBusDevice
	void Block() final;
	void Unblock() final;
	void ContSend() final;

public:
	struct ScsiDeviceImpl *waiting_next {};
	struct ScsiDeviceImpl *waiting_prev {};

	bool manual_autosense : 1 {};	// no autosense support
	bool is_atapi : 1 {};			// ATAPI device - needs some commands emulated

	int lock_count {};				// sum of blocked[0..1] and sim_overflow
	int blocked[2] {};				// depth of nested locks by bus manager (0) and SIM (1)
	int sim_overflow {};			// 1, if SIM returned a request because of device queue overflow
	int left_slots {};				// left command queuing slots for device
	int total_slots {};				// total number of command queuing slots for device

	ScsiCcbImpl *queued_reqs {};	// queued requests, circularly doubly linked
									// (scsi_insert_new_request depends on circular)

	int64 last_sort {};				// last sort value (for elevator sort)
	int32 valid {};					// access must be atomic!

	ScsiBusImpl *bus {};
	uchar target_id {};
	uchar target_lun {};

	ScsiCcbImpl *auto_sense_request {};		// auto-sense request
	ScsiCcbImpl *auto_sense_originator {};	// request that auto-sense is
										// currently requested for
	area_id auto_sense_area {};			// area of auto-sense data and S/G list

	uint8 emulation_map[256/8] {};		// bit field with index being command code:
								// 1 indicates that this command is not supported
								// and thus must be emulated

	scsi_res_inquiry inquiry_data {};
	DeviceNode *node {};	// device node

	struct mutex dma_buffer_lock {};	// lock between DMA buffer user and clean-up daemon
	sem_id dma_buffer_owner {};	// to be acquired before using DMA buffer
	struct dma_buffer dma_buffer {};	// DMA buffer

	// buffer used for emulating SCSI commands
	char *buffer {};
	physical_entry *buffer_sg_list {};
	size_t buffer_sg_count {};
	size_t buffer_size {};
	area_id buffer_area {};
	sem_id buffer_sem {};
};

enum {
	ev_scsi_requeue_request = 1,
	ev_scsi_resubmit_request,
	ev_scsi_submit_autosense,
	ev_scsi_finish_autosense,
	ev_scsi_device_queue_overflow,
	ev_scsi_request_finished,
	ev_scsi_async_io,
	ev_scsi_do_resend_request,
	ev_copy_sg_data
};

// check whether device is in bus's wait queue
// we use the fact the queue is circular, so we don't need an explicit flag
#define DEVICE_IN_WAIT_QUEUE( device ) ((device)->waiting_next != NULL)


// state of ccb
enum {
	SCSI_STATE_FREE = 0,
	SCSI_STATE_INWORK = 1,
	SCSI_STATE_QUEUED = 2,
	SCSI_STATE_SENT = 3,
	SCSI_STATE_FINISHED = 5,
};


extern locked_pool_interface *locked_pool;

extern driver_module_info scsi_bus_module;


__BEGIN_DECLS


// ccb.c
status_t scsi_init_ccb_alloc(ScsiBusImpl *bus);
void scsi_uninit_ccb_alloc(ScsiBusImpl *bus);


// devices.c
status_t scsi_force_get_device(ScsiBusImpl *bus,
	uchar target_id, uchar target_lun, ScsiDeviceImpl **res_device);
void scsi_put_forced_device(ScsiDeviceImpl *device);
status_t scsi_register_device(ScsiBusImpl *bus, uchar target_id,
	uchar target_lun, scsi_res_inquiry *inquiry_data);


// device_scan.c
status_t scsi_scan_bus(ScsiBusImpl *bus);
status_t scsi_scan_lun(ScsiBusImpl *bus, uchar target_id, uchar target_lun);


// dpc.c
bool scsi_check_exec_dpc(ScsiBusImpl *bus);

// scsi_io.c
bool scsi_check_exec_service(ScsiBusImpl *bus);

void scsi_requeue_request(ScsiCcb *request, bool bus_overflow);
void scsi_resubmit_request(ScsiCcb *request);
void scsi_request_finished(ScsiCcb *request, uint num_requests);


// scatter_gather.c
bool create_temp_sg(ScsiCcb *ccb);
void cleanup_tmp_sg(ScsiCcb *ccb);

int init_temp_sg(void);
void uninit_temp_sg(void);


// dma_buffer.c
void scsi_dma_buffer_daemon(void *dev, int counter);
void scsi_release_dma_buffer(ScsiCcbImpl *request);
bool scsi_get_dma_buffer(ScsiCcbImpl *request);
void scsi_dma_buffer_free(dma_buffer *buffer);
void scsi_dma_buffer_init(dma_buffer *buffer);


// emulation.c
bool scsi_start_emulation(ScsiCcb *request);
void scsi_finish_emulation(ScsiCcb *request);
void scsi_free_emulation_buffer(ScsiDeviceImpl *device);
status_t scsi_init_emulation_buffer(ScsiDeviceImpl *device, size_t buffer_size);


__END_DECLS


#endif	/* _SCSI_INTERNAL_H */
