/*
 * Copyright 2019-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 */


#include <dm2/bus/PCI.h>

#include <condition_variable.h>
#include <AutoDeleter.h>
#include <kernel.h>
#include <smp.h>
#include <fs/devfs.h>
#include <util/AutoLock.h>

#include "IORequest.h"

extern "C" {
#include <libnvme/nvme.h>
#include <libnvme/nvme_internal.h>
}


//#define TRACE_NVME_DISK
#ifdef TRACE_NVME_DISK
#	define TRACE(x...) dprintf("nvme_disk: " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("nvme_disk: " x)
#define TRACE_ERROR(x...)	dprintf("\33[33mnvme_disk:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


static const uint8 kDriveIcon[] = {
	0x6e, 0x63, 0x69, 0x66, 0x08, 0x03, 0x01, 0x00, 0x00, 0x02, 0x00, 0x16,
	0x02, 0x3c, 0xc7, 0xee, 0x38, 0x9b, 0xc0, 0xba, 0x16, 0x57, 0x3e, 0x39,
	0xb0, 0x49, 0x77, 0xc8, 0x42, 0xad, 0xc7, 0x00, 0xff, 0xff, 0xd3, 0x02,
	0x00, 0x06, 0x02, 0x3c, 0x96, 0x32, 0x3a, 0x4d, 0x3f, 0xba, 0xfc, 0x01,
	0x3d, 0x5a, 0x97, 0x4b, 0x57, 0xa5, 0x49, 0x84, 0x4d, 0x00, 0x47, 0x47,
	0x47, 0xff, 0xa5, 0xa0, 0xa0, 0x02, 0x00, 0x16, 0x02, 0xbc, 0x59, 0x2f,
	0xbb, 0x29, 0xa7, 0x3c, 0x0c, 0xe4, 0xbd, 0x0b, 0x7c, 0x48, 0x92, 0xc0,
	0x4b, 0x79, 0x66, 0x00, 0x7d, 0xff, 0xd4, 0x02, 0x00, 0x06, 0x02, 0x38,
	0xdb, 0xb4, 0x39, 0x97, 0x33, 0xbc, 0x4a, 0x33, 0x3b, 0xa5, 0x42, 0x48,
	0x6e, 0x66, 0x49, 0xee, 0x7b, 0x00, 0x59, 0x67, 0x56, 0xff, 0xeb, 0xb2,
	0xb2, 0x03, 0xa7, 0xff, 0x00, 0x03, 0xff, 0x00, 0x00, 0x04, 0x01, 0x80,
	0x07, 0x0a, 0x06, 0x22, 0x3c, 0x22, 0x49, 0x44, 0x5b, 0x5a, 0x3e, 0x5a,
	0x31, 0x39, 0x25, 0x0a, 0x04, 0x22, 0x3c, 0x44, 0x4b, 0x5a, 0x31, 0x39,
	0x25, 0x0a, 0x04, 0x44, 0x4b, 0x44, 0x5b, 0x5a, 0x3e, 0x5a, 0x31, 0x0a,
	0x04, 0x22, 0x3c, 0x22, 0x49, 0x44, 0x5b, 0x44, 0x4b, 0x08, 0x02, 0x27,
	0x43, 0xb8, 0x14, 0xc1, 0xf1, 0x08, 0x02, 0x26, 0x43, 0x29, 0x44, 0x0a,
	0x05, 0x44, 0x5d, 0x49, 0x5d, 0x60, 0x3e, 0x5a, 0x3b, 0x5b, 0x3f, 0x08,
	0x0a, 0x07, 0x01, 0x06, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x10, 0x01, 0x17,
	0x84, 0x00, 0x04, 0x0a, 0x01, 0x01, 0x01, 0x00, 0x0a, 0x02, 0x01, 0x02,
	0x00, 0x0a, 0x03, 0x01, 0x03, 0x00, 0x0a, 0x04, 0x01, 0x04, 0x10, 0x01,
	0x17, 0x85, 0x20, 0x04, 0x0a, 0x06, 0x01, 0x05, 0x30, 0x24, 0xb3, 0x99,
	0x01, 0x17, 0x82, 0x00, 0x04, 0x0a, 0x05, 0x01, 0x05, 0x30, 0x20, 0xb2,
	0xe6, 0x01, 0x17, 0x82, 0x00, 0x04
};


#define NVME_DISK_DRIVER_MODULE_NAME "drivers/disk/nvme_disk/driver/v1"

#define NVME_MAX_QPAIRS (16)


class NvmeDiskDriver;


struct nvme_io_request {
	status_t status;

	bool write;

	off_t lba_start;
	size_t lba_count;

	physical_entry* iovecs;
	int32 iovec_count;

	int32 iovec_i;
	uint32 iovec_offset;
};


class NvmeDiskDevFsNodeHandle: public DevFsNodeHandle {
public:
	NvmeDiskDevFsNodeHandle(NvmeDiskDriver& driver): fDriver(driver) {}
	virtual ~NvmeDiskDevFsNodeHandle() = default;

	void Free() final {delete this;}
	status_t Read(off_t pos, void* buffer, size_t* length) final;
	status_t Write(off_t pos, const void* buffer, size_t* length) final;
	status_t IO(io_request *request) final;
	status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;

private:
	status_t GetGeometry(device_geometry* geometry);
	status_t BouncedIO(io_request *request);

private:
	NvmeDiskDriver& fDriver;
};


class NvmeDiskDevFsNode: public DevFsNode {
public:
	NvmeDiskDevFsNode(NvmeDiskDriver& driver): fDriver(driver) {}
	virtual ~NvmeDiskDevFsNode() = default;

	Capabilities GetCapabilities() const final;
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

private:
	NvmeDiskDriver& fDriver;
};


class NvmeDiskDriver: public DeviceDriver {
public:
	NvmeDiskDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~NvmeDiskDriver();

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	struct qpair_info {
		struct nvme_qpair* qpair;
	};

	status_t Init();
	void SetCapacity(uint64 capacity, uint32 blockSize);
	static int32 InterruptHandler(void* cookie);
	qpair_info* GetQpair();
	void AwaitStatus(struct nvme_qpair* qpair, status_t& status);
	status_t DoIoRequest(nvme_io_request* request);
	status_t Flush();
	status_t Trim(fs_trim_data* trimData);

private:
	friend class NvmeDiskDevFsNode;
	friend class NvmeDiskDevFsNodeHandle;

	DeviceNode*				fNode;
	pci_info				fInfo {};

	struct nvme_ctrlr*		fCtrlr {};

	struct nvme_ns*			fNs {};
	uint64					fCapacity {};
	uint32					fBlockSize {};
	uint32					fMaxIoBlocks {};
	status_t				fMediaStatus {};

	DMAResource				fDmaResource;
	sem_id					fDmaBuffersSem = -1;

	rw_lock					fRoundedWriteLock {};

	ConditionVariable		fInterrupt {};
	int32					fPolling {};

	qpair_info				fQpairs[NVME_MAX_QPAIRS] {};
	uint32					fQpairCount {};

	PciDevice*				fPciDevice {};
	NvmeDiskDevFsNode		fDevFsNode;
};


static void
io_finished_callback(status_t* status, const struct nvme_cpl* cpl)
{
	*status = nvme_cpl_is_error(cpl) ? B_IO_ERROR : B_OK;
}


static void
ior_reset_sgl(nvme_io_request* request, uint32_t offset)
{
	TRACE("IOR Reset: %" B_PRIu32 "\n", offset);

	int32 i = 0;
	while (offset > 0 && request->iovecs[i].size <= offset) {
		offset -= request->iovecs[i].size;
		i++;
	}
	request->iovec_i = i;
	request->iovec_offset = offset;
}


static int
ior_next_sge(nvme_io_request* request, uint64_t* address, uint32_t* length)
{
	int32 index = request->iovec_i;
	if (index < 0 || index > request->iovec_count)
		return -1;

	*address = request->iovecs[index].address + request->iovec_offset;
	*length = request->iovecs[index].size - request->iovec_offset;

	TRACE("IOV %d (+ " B_PRIu32 "): 0x%" B_PRIx64 ", %" B_PRIu32 "\n",
		request->iovec_i, request->iovec_offset, *address, *length);

	request->iovec_i++;
	request->iovec_offset = 0;
	return 0;
}


// #pragma mark - NvmeDiskDriver

status_t
NvmeDiskDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<NvmeDiskDriver> driver(new(std::nothrow) NvmeDiskDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t NvmeDiskDriver::Init()
{
	CALLED();

	CHECK_RET(nvme_lib_init((enum nvme_log_level)0, (enum nvme_log_facility)0, NULL));

	fMediaStatus = B_OK;

	fPciDevice = fNode->QueryBusInterface<PciDevice>();
	fPciDevice->GetPciInfo(&fInfo);

	// construct the libnvme pci_device struct
	pci_device* device = new pci_device;
	device->vendor_id = fInfo.vendor_id;
	device->device_id = fInfo.device_id;
	device->subvendor_id = 0;
	device->subdevice_id = 0;

	device->domain = 0;
	device->bus = fInfo.bus;
	device->dev = fInfo.device;
	device->func = fInfo.function;

	device->pci_info = &fInfo;

	// enable busmaster and memory mapped access
	uint16 command = fPciDevice->ReadPciConfig(PCI_command, 2);
	command |= PCI_command_master | PCI_command_memory;
	fPciDevice->WritePciConfig(PCI_command, 2, command);

	// open the controller
	fCtrlr = nvme_ctrlr_open(device, NULL);
	if (fCtrlr == NULL) {
		TRACE_ERROR("failed to open the controller!\n");
		return B_ERROR;
	}

	struct nvme_ctrlr_stat cstat;
	int err = nvme_ctrlr_stat(fCtrlr, &cstat);
	if (err != 0) {
		TRACE_ERROR("failed to get controller information!\n");
		nvme_ctrlr_close(fCtrlr);
		return err;
	}

	TRACE_ALWAYS("attached to NVMe device \"%s (%s)\"\n", cstat.mn, cstat.sn);
	TRACE_ALWAYS("\tmaximum transfer size: %" B_PRIuSIZE "\n", cstat.max_xfer_size);
	TRACE_ALWAYS("\tqpair count: %d\n", cstat.io_qpairs);

	// TODO: export more than just the first namespace!
	fNs = nvme_ns_open(fCtrlr, cstat.ns_ids[0]);
	if (fNs == NULL) {
		TRACE_ERROR("failed to open namespace!\n");
		nvme_ctrlr_close(fCtrlr);
		return B_ERROR;
	}
	TRACE_ALWAYS("namespace 0\n");

	struct nvme_ns_stat nsstat;
	err = nvme_ns_stat(fNs, &nsstat);
	if (err != 0) {
		TRACE_ERROR("failed to get namespace information!\n");
		nvme_ctrlr_close(fCtrlr);
		return err;
	}

	// store capacity information
	TRACE_ALWAYS("\tblock size: %" B_PRIuSIZE ", stripe size: %u\n",
		nsstat.sector_size, fNs->stripe_size);
	SetCapacity(nsstat.sectors, nsstat.sector_size);

	command = fPciDevice->ReadPciConfig(PCI_command, 2);
	command &= ~(PCI_command_int_disable);
	fPciDevice->WritePciConfig(PCI_command, 2, command);

	uint8 irq = fInfo.u.h0.interrupt_line;
	if (fPciDevice->GetMsixCount()) {
		uint8 msixVector = 0;
		if (fPciDevice->ConfigureMsix(1, &msixVector) == B_OK
			&& fPciDevice->EnableMsix() == B_OK) {
			TRACE_ALWAYS("using MSI-X\n");
			irq = msixVector;
		}
	} else if (fPciDevice->GetMsiCount() >= 1) {
		uint8 msiVector = 0;
		if (fPciDevice->ConfigureMsi(1, &msiVector) == B_OK
			&& fPciDevice->EnableMsi() == B_OK) {
			TRACE_ALWAYS("using message signaled interrupts\n");
			irq = msiVector;
		}
	}

	if (irq == 0 || irq == 0xFF) {
		TRACE_ERROR("device PCI:%d:%d:%d was assigned an invalid IRQ\n",
			fInfo.bus, fInfo.device, fInfo.function);
		fPolling = 1;
	} else {
		fPolling = 0;
	}
	fInterrupt.Init(NULL, NULL);
	install_io_interrupt_handler(irq, InterruptHandler, (void*)this, B_NO_HANDLED_INFO);

	if (fCtrlr->feature_supported[NVME_FEAT_INTERRUPT_COALESCING]) {
		uint32 microseconds = 16, threshold = 32;
		nvme_admin_set_feature(fCtrlr, false, NVME_FEAT_INTERRUPT_COALESCING,
			((microseconds / 100) << 8) | threshold, 0, NULL);
	}

	// allocate qpairs
	uint32 try_qpairs = cstat.io_qpairs;
	try_qpairs = min_c(try_qpairs, NVME_MAX_QPAIRS);
	if (try_qpairs >= (uint32)smp_get_num_cpus()) {
		try_qpairs = smp_get_num_cpus();
	} else {
		// Find the highest number of qpairs that evenly divides the number of CPUs.
		while ((smp_get_num_cpus() % try_qpairs) != 0)
			try_qpairs--;
	}
	fQpairCount = 0;
	for (uint32 i = 0; i < try_qpairs; i++) {
		fQpairs[i].qpair = nvme_ioqp_get(fCtrlr,
			(enum nvme_qprio)0, 0);
		if (fQpairs[i].qpair == NULL)
			break;

		fQpairCount++;
	}
	if (fQpairCount == 0) {
		TRACE_ERROR("failed to allocate qpairs!\n");
		nvme_ctrlr_close(fCtrlr);
		return B_NO_MEMORY;
	}
	if (fQpairCount != try_qpairs) {
		TRACE_ALWAYS("warning: did not get expected number of qpairs\n");
	}

	// allocate DMA buffers
	int buffers = fQpairCount * 2;

	dma_restrictions restrictions = {};
	restrictions.alignment = B_PAGE_SIZE;
		// Technically, the first and last segments in a transfer can be aligned
		// only on 32-bits, and the rest only need to have sizes that are a multiple
		// of the block size.
	restrictions.max_segment_count = (NVME_MAX_SGL_DESCRIPTORS / 2);
	restrictions.max_transfer_size = cstat.max_xfer_size;
	fMaxIoBlocks = cstat.max_xfer_size / nsstat.sector_size;

	err = fDmaResource.Init(restrictions, B_PAGE_SIZE, buffers, buffers);
	if (err != 0) {
		TRACE_ERROR("failed to initialize DMA resource!\n");
		nvme_ctrlr_close(fCtrlr);
		return err;
	}

	fDmaBuffersSem = create_sem(buffers, "nvme buffers sem");
	if (fDmaBuffersSem < 0) {
		TRACE_ERROR("failed to create DMA buffers semaphore!\n");
		nvme_ctrlr_close(fCtrlr);
		return fDmaBuffersSem;
	}

	// set up rounded-write lock
	rw_lock_init(&fRoundedWriteLock, "nvme rounded writes");

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "disk/nvme/%" B_PRId32 "/raw", id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


NvmeDiskDriver::~NvmeDiskDriver()
{
	CALLED();
	remove_io_interrupt_handler(fInfo.u.h0.interrupt_line,
		InterruptHandler, (void*)this);

	rw_lock_destroy(&fRoundedWriteLock);

	nvme_ns_close(fNs);
	nvme_ctrlr_close(fCtrlr);

	// TODO: Deallocate MSI(-X).
	// TODO: Deallocate PCI.
}


void
NvmeDiskDriver::SetCapacity(uint64 capacity, uint32 blockSize)
{
	TRACE("SetCapacity(device = %p, capacity = %" B_PRIu64 ", blockSize = %" B_PRIu32 ")\n",
		info, capacity, blockSize);

	fCapacity = capacity;
	fBlockSize = blockSize;
}


int32
NvmeDiskDriver::InterruptHandler(void* cookie)
{
	NvmeDiskDriver* driver = (NvmeDiskDriver*)cookie;
	driver->fInterrupt.NotifyAll();
	driver->fPolling = -1;
	return 0;
}


NvmeDiskDriver::qpair_info*
NvmeDiskDriver::GetQpair()
{
	return &fQpairs[smp_get_current_cpu() % fQpairCount];
}


void
NvmeDiskDriver::AwaitStatus(struct nvme_qpair* qpair, status_t& status)
{
	CALLED();

	ConditionVariableEntry entry;
	int timeouts = 0;
	while (status == EINPROGRESS) {
		fInterrupt.Add(&entry);

		nvme_qpair_poll(qpair, 0);

		if (status != EINPROGRESS)
			return;

		if (fPolling > 0) {
			entry.Wait(B_RELATIVE_TIMEOUT, min_c(5 * 1000 * 1000,
				(1 << timeouts) * 1000));
			timeouts++;
		} else if (entry.Wait(B_RELATIVE_TIMEOUT, 5 * 1000 * 1000) != B_OK) {
			// This should never happen, as we are woken up on every interrupt
			// no matter the qpair or transfer within; so if it does occur,
			// that probably means the controller stalled, or maybe cannot
			// generate interrupts at all.

			TRACE_ERROR("timed out waiting for interrupt!\n");
			if (timeouts++ >= 3) {
				nvme_qpair_fail(qpair);
				status = B_TIMED_OUT;
				return;
			}

			fPolling++;
			if (fPolling > 0) {
				TRACE_ALWAYS("switching to polling mode, performance will be affected!\n");
			}
		}

		nvme_qpair_poll(qpair, 0);
	}
}


status_t
NvmeDiskDriver::DoIoRequest(nvme_io_request *request)
{
	request->status = EINPROGRESS;

	qpair_info* qpinfo = GetQpair();
	int ret = -1;
	if (request->write) {
		ret = nvme_ns_writev(fNs, qpinfo->qpair, request->lba_start,
			request->lba_count, (nvme_cmd_cb)io_finished_callback, request,
			0, (nvme_req_reset_sgl_cb)ior_reset_sgl,
			(nvme_req_next_sge_cb)ior_next_sge);
	} else {
		ret = nvme_ns_readv(fNs, qpinfo->qpair, request->lba_start,
			request->lba_count, (nvme_cmd_cb)io_finished_callback, request,
			0, (nvme_req_reset_sgl_cb)ior_reset_sgl,
			(nvme_req_next_sge_cb)ior_next_sge);
	}
	if (ret != 0) {
		TRACE_ERROR("attempt to queue %s I/O at LBA %" B_PRIdOFF " of %" B_PRIuSIZE
			" blocks failed!\n", request->write ? "write" : "read",
			request->lba_start, request->lba_count);

		request->lba_count = 0;
		return ret;
	}

	AwaitStatus(qpinfo->qpair, request->status);

	if (request->status != B_OK) {
		TRACE_ERROR("%s at LBA %" B_PRIdOFF " of %" B_PRIuSIZE
			" blocks failed!\n", request->write ? "write" : "read",
			request->lba_start, request->lba_count);

		request->lba_count = 0;
	}
	return request->status;
}


status_t
NvmeDiskDriver::Flush()
{
	CALLED();
	status_t status = EINPROGRESS;

	qpair_info* qpinfo = GetQpair();
	int ret = nvme_ns_flush(fNs, qpinfo->qpair,
		(nvme_cmd_cb)io_finished_callback, &status);
	if (ret != 0)
		return ret;

	AwaitStatus(qpinfo->qpair, status);
	return status;
}


status_t
NvmeDiskDriver::Trim(fs_trim_data* trimData)
{
	CALLED();
	status_t status = EINPROGRESS;

	qpair_info* qpinfo = GetQpair();
	int ret = nvme_ns_flush(fNs, qpinfo->qpair,
		(nvme_cmd_cb)io_finished_callback, &status);
	if (ret != 0)
		return ret;

	AwaitStatus(qpinfo->qpair, status);
	return status;
}


// #pragma mark - NvmeDiskDevFsNode

DevFsNode::Capabilities
NvmeDiskDevFsNode::GetCapabilities() const
{
	return {
		.read = true,
		.write = true,
		.io = true,
		.control = true
	};
}


status_t
NvmeDiskDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	CALLED();

	ObjectDeleter<NvmeDiskDevFsNodeHandle> handle(new(std::nothrow) NvmeDiskDevFsNodeHandle(fDriver));
	if (!handle.IsSet())
		return B_NO_MEMORY;

	*outHandle = handle.Detach();
	return B_OK;
}


// #pragma mark - NvmeDiskDevFsNodeHandle

status_t
NvmeDiskDevFsNodeHandle::GetGeometry(device_geometry* geometry)
{
	devfs_compute_geometry_size(geometry, fDriver.fCapacity, fDriver.fBlockSize);
	geometry->bytes_per_physical_sector = fDriver.fBlockSize;

	geometry->device_type = B_DISK;
	geometry->removable = false;

	geometry->read_only = false;
	geometry->write_once = false;

	TRACE("get_geometry(): %" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 ", %d, %d, %d, %d\n",
		geometry->bytes_per_sector, geometry->sectors_per_track,
		geometry->cylinder_count, geometry->head_count, geometry->device_type,
		geometry->removable, geometry->read_only, geometry->write_once);

	return B_OK;
}


status_t
NvmeDiskDevFsNodeHandle::BouncedIO(io_request *request)
{
	CALLED();

	WriteLocker writeLocker;
	if (request->IsWrite())
		writeLocker.SetTo(fDriver.fRoundedWriteLock, false);

	status_t status = acquire_sem(fDriver.fDmaBuffersSem);
	if (status != B_OK) {
		request->SetStatusAndNotify(status);
		return status;
	}

	const size_t block_size = fDriver.fBlockSize;

	TRACE("%p: IOR Offset: %" B_PRIdOFF "; Length %" B_PRIuGENADDR
		"; Write %s\n", request, request->Offset(), request->Length(),
		request->IsWrite() ? "yes" : "no");

	nvme_io_request nvme_request;
	while (request->RemainingBytes() > 0) {
		IOOperation operation;
		status = fDriver.fDmaResource.TranslateNext(request, &operation, 0);
		if (status != B_OK)
			break;

		do {
			TRACE("%p: IOO offset: %" B_PRIdOFF ", length: %" B_PRIuGENADDR
				", write: %s\n", request, operation.Offset(),
				operation.Length(), operation.IsWrite() ? "yes" : "no");

			nvme_request.write = operation.IsWrite();
			nvme_request.lba_start = operation.Offset() / block_size;
			nvme_request.lba_count = operation.Length() / block_size;
			nvme_request.iovecs = (physical_entry*)operation.Vecs();
			nvme_request.iovec_count = operation.VecCount();

			status = fDriver.DoIoRequest(&nvme_request);

			operation.SetStatus(status,
				status == B_OK ? operation.Length() : 0);
		} while (status == B_OK && !operation.Finish());

		if (status == B_OK && operation.Status() != B_OK) {
			TRACE_ERROR("I/O succeeded but IOOperation failed!\n");
			status = operation.Status();
		}

		request->OperationFinished(&operation);

		fDriver.fDmaResource.RecycleBuffer(operation.Buffer());

		TRACE("%p: status %s, remaining bytes %" B_PRIuGENADDR "\n", request,
			strerror(status), request->RemainingBytes());
		if (status != B_OK)
			break;
	}

	release_sem(fDriver.fDmaBuffersSem);

	// Notify() also takes care of UnlockMemory().
	if (status != B_OK && request->Status() == B_OK)
		request->SetStatusAndNotify(status);
	else
		request->NotifyFinished();
	return status;
}


status_t
NvmeDiskDevFsNodeHandle::IO(io_request *request)
{
	CALLED();

	const off_t ns_end = (fDriver.fCapacity * fDriver.fBlockSize);
	if ((request->Offset() + (off_t)request->Length()) > ns_end)
		return ERANGE;

	nvme_io_request nvme_request;
	memset(&nvme_request, 0, sizeof(nvme_io_request));

	nvme_request.write = request->IsWrite();

	physical_entry* vtophys = NULL;
	MemoryDeleter vtophysDeleter;

	IOBuffer* buffer = request->Buffer();
	status_t status = B_OK;
	if (!buffer->IsPhysical()) {
		status = buffer->LockMemory(request->TeamID(), request->IsWrite());
		if (status != B_OK) {
			TRACE_ERROR("failed to lock memory: %s\n", strerror(status));
			return status;
		}
		// SetStatusAndNotify() takes care of unlocking memory if necessary.

		// This is slightly inefficient, as we could use a BStackOrHeapArray in
		// the optimal case (few physical entries required), but we would not
		// know whether or not that was possible until calling get_memory_map()
		// and then potentially reallocating, which would complicate the logic.

		int32 vtophys_length = (request->Length() / B_PAGE_SIZE) + 2;
		nvme_request.iovecs = vtophys = (physical_entry*)malloc(sizeof(physical_entry)
			* vtophys_length);
		if (vtophys == NULL) {
			TRACE_ERROR("failed to allocate memory for iovecs\n");
			request->SetStatusAndNotify(B_NO_MEMORY);
			return B_NO_MEMORY;
		}
		vtophysDeleter.SetTo(vtophys);

		for (size_t i = 0; i < buffer->VecCount(); i++) {
			generic_io_vec virt = buffer->VecAt(i);
			uint32 entries = vtophys_length - nvme_request.iovec_count;

			// Avoid copies by going straight into the vtophys array.
			status = get_memory_map_etc(request->TeamID(), (void*)virt.base,
				virt.length, vtophys + nvme_request.iovec_count, &entries);
			if (status == B_BUFFER_OVERFLOW) {
				TRACE("vtophys array was too small, reallocating\n");

				vtophysDeleter.Detach();
				vtophys_length *= 2;
				nvme_request.iovecs = vtophys = (physical_entry*)realloc(vtophys,
					sizeof(physical_entry) * vtophys_length);
				vtophysDeleter.SetTo(vtophys);
				if (vtophys == NULL) {
					status = B_NO_MEMORY;
				} else {
					// Try again, with the larger buffer this time.
					i--;
					continue;
				}
			}
			if (status != B_OK) {
				TRACE_ERROR("I/O get_memory_map failed: %s\n", strerror(status));
				request->SetStatusAndNotify(status);
				return status;
			}

			nvme_request.iovec_count += entries;
		}
	} else {
		nvme_request.iovecs = (physical_entry*)buffer->Vecs();
		nvme_request.iovec_count = buffer->VecCount();
	}

	// See if we need to bounce anything other than the first or last vec.
	const size_t block_size = fDriver.fBlockSize;
	bool bounceAll = false;
	for (int32 i = 1; !bounceAll && i < (nvme_request.iovec_count - 1); i++) {
		if ((nvme_request.iovecs[i].address % B_PAGE_SIZE) != 0)
			bounceAll = true;
		if ((nvme_request.iovecs[i].size % B_PAGE_SIZE) != 0)
			bounceAll = true;
	}

	// See if we need to bounce due to the first or last vecs.
	if (nvme_request.iovec_count > 1) {
		// There are middle vecs, so the first and last vecs have different restrictions: they
		// need only be a multiple of the block size, and must end and start on a page boundary,
		// respectively, though the start address must always be 32-bit-aligned.
		physical_entry* entry = &nvme_request.iovecs[0];
		if (!bounceAll && (((entry->address + entry->size) % B_PAGE_SIZE) != 0
				|| (entry->address & 0x3) != 0 || (entry->size % block_size) != 0))
			bounceAll = true;

		entry = &nvme_request.iovecs[nvme_request.iovec_count - 1];
		if (!bounceAll && ((entry->address % B_PAGE_SIZE) != 0
				|| (entry->size % block_size) != 0))
			bounceAll = true;
	} else {
		// There is only one vec. Check that it is a multiple of the block size,
		// and that its address is 32-bit-aligned.
		physical_entry* entry = &nvme_request.iovecs[0];
		if (!bounceAll && ((entry->address & 0x3) != 0 || (entry->size % block_size) != 0))
			bounceAll = true;
	}

	// See if we need to bounce due to rounding.
	const off_t rounded_pos = ROUNDDOWN(request->Offset(), block_size);
	phys_size_t rounded_len = ROUNDUP(request->Length() + (request->Offset()
		- rounded_pos), block_size);
	if (rounded_pos != request->Offset() || rounded_len != request->Length())
		bounceAll = true;

	if (bounceAll) {
		// Let the bounced I/O routine take care of everything from here.
		return BouncedIO(request);
	}

	nvme_request.lba_start = rounded_pos / block_size;
	nvme_request.lba_count = rounded_len / block_size;

	// No bouncing was required.
	ReadLocker readLocker;
	if (nvme_request.write)
		readLocker.SetTo(fDriver.fRoundedWriteLock, false);

	// Error check before actually doing I/O.
	if (status != B_OK) {
		TRACE_ERROR("I/O failed early: %s\n", strerror(status));
		request->SetStatusAndNotify(status);
		return status;
	}

	const uint32 max_io_blocks = fDriver.fMaxIoBlocks;
	int32 remaining = nvme_request.iovec_count;
	while (remaining > 0) {
		nvme_request.iovec_count = min_c(remaining,
			NVME_MAX_SGL_DESCRIPTORS / 2);

		nvme_request.lba_count = 0;
		for (int i = 0; i < nvme_request.iovec_count; i++) {
			uint32 new_lba_count = nvme_request.lba_count
				+ (nvme_request.iovecs[i].size / block_size);
			if (nvme_request.lba_count > 0 && new_lba_count > max_io_blocks) {
				// We already have a nonzero length, and adding this vec would
				// make us go over (or we already are over.) Stop adding.
				nvme_request.iovec_count = i;
				break;
			}

			nvme_request.lba_count = new_lba_count;
		}

		status = fDriver.DoIoRequest(&nvme_request);
		if (status != B_OK)
			break;

		nvme_request.iovecs += nvme_request.iovec_count;
		remaining -= nvme_request.iovec_count;
		nvme_request.lba_start += nvme_request.lba_count;
	}

	if (status != B_OK)
		TRACE_ERROR("I/O failed: %s\n", strerror(status));

	request->SetTransferredBytes(status != B_OK,
		(nvme_request.lba_start * block_size) - rounded_pos);
	request->SetStatusAndNotify(status);
	return status;
}


status_t
NvmeDiskDevFsNodeHandle::Read(off_t pos, void* buffer, size_t* length)
{
	CALLED();

	const off_t ns_end = (fDriver.fCapacity * fDriver.fBlockSize);
	if (pos >= ns_end)
		return B_BAD_VALUE;
	if ((pos + (off_t)*length) > ns_end)
		*length = ns_end - pos;

	IORequest request;
	status_t status = request.Init(pos, (addr_t)buffer, *length, false, 0);
	if (status != B_OK)
		return status;

	status = IO(&request);
	*length = request.TransferredBytes();
	return status;
}


status_t
NvmeDiskDevFsNodeHandle::Write(off_t pos, const void* buffer, size_t* length)
{
	CALLED();

	const off_t ns_end = (fDriver.fCapacity * fDriver.fBlockSize);
	if (pos >= ns_end)
		return B_BAD_VALUE;
	if ((pos + (off_t)*length) > ns_end)
		*length = ns_end - pos;

	IORequest request;
	status_t status = request.Init(pos, (addr_t)buffer, *length, true, 0);
	if (status != B_OK)
		return status;

	status = IO(&request);
	*length = request.TransferredBytes();
	return status;
}


status_t
NvmeDiskDevFsNodeHandle::Control(uint32 op, void *buffer, size_t length, bool isKernel)
{
	CALLED();

	TRACE("ioctl(op = %" B_PRId32 ")\n", op);

	switch (op) {
		case B_GET_MEDIA_STATUS:
		{
			*(status_t *)buffer = fDriver.fMediaStatus;
			fDriver.fMediaStatus = B_OK;
			return B_OK;
			break;
		}

		case B_GET_DEVICE_SIZE:
		{
			size_t size = fDriver.fCapacity * fDriver.fBlockSize;
			return user_memcpy(buffer, &size, sizeof(size_t));
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == NULL || length > sizeof(device_geometry))
				return B_BAD_VALUE;

		 	device_geometry geometry;
			status_t status = GetGeometry(&geometry);
			if (status != B_OK)
				return status;

			return user_memcpy(buffer, &geometry, length);
		}

		case B_GET_ICON_NAME:
			return user_strlcpy((char*)buffer, "devices/drive-harddisk",
				B_FILE_NAME_LENGTH);

		case B_GET_VECTOR_ICON:
		{
			device_icon iconData;
			if (length != sizeof(device_icon))
				return B_BAD_VALUE;
			if (user_memcpy(&iconData, buffer, sizeof(device_icon)) != B_OK)
				return B_BAD_ADDRESS;

			if (iconData.icon_size >= (int32)sizeof(kDriveIcon)) {
				if (user_memcpy(iconData.icon_data, kDriveIcon,
						sizeof(kDriveIcon)) != B_OK)
					return B_BAD_ADDRESS;
			}

			iconData.icon_size = sizeof(kDriveIcon);
			return user_memcpy(buffer, &iconData, sizeof(device_icon));
		}

		case B_FLUSH_DRIVE_CACHE:
			return fDriver.Flush();

		case B_TRIM_DEVICE:
			ASSERT(IS_KERNEL_ADDRESS(buffer));
			return fDriver.Trim((fs_trim_data*)buffer);
	}

	return B_DEV_INVALID_IOCTL;
}


static driver_module_info sNvmeDiskDriver = {
	.info = {
		.name = NVME_DISK_DRIVER_MODULE_NAME,
	},
	.probe = NvmeDiskDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sNvmeDiskDriver,
	NULL
};
