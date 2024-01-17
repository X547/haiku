/*
 * Copyright 2011-2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 *		Jian Chiang <j.jian.chiang@gmail.com>
 *		Jérôme Duval <jerome.duval@gmail.com>
 *		Akshay Jaggi <akshay1994.leo@gmail.com>
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Alexander von Gluck <kallisti5@unixzen.com>
 */


#include <vm/vm.h>

#include "xhci.h"

#include <string.h>

#include <algorithm>

#include <ByteOrder.h>

#include <AutoDeleter.h>
#include <ScopeExit.h>
#include <util/AutoLock.h>
#include <util/iovec_support.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define CALLED(x...)	TRACE_MODULE("CALLED %s\n", __PRETTY_FUNCTION__)

#define USB_MODULE_NAME	"xhci"


#define XHCI_DRIVER_MODULE_NAME "busses/usb/xhci/driver/v1"


static const char*
xhci_error_string(uint32 error)
{
	switch (error) {
		case COMP_INVALID: return "Invalid";
		case COMP_SUCCESS: return "Success";
		case COMP_DATA_BUFFER: return "Data buffer";
		case COMP_BABBLE: return "Babble detected";
		case COMP_USB_TRANSACTION: return "USB transaction";
		case COMP_TRB: return "TRB";
		case COMP_STALL: return "Stall";
		case COMP_RESOURCE: return "Resource";
		case COMP_BANDWIDTH: return "Bandwidth";
		case COMP_NO_SLOTS: return "No slots";
		case COMP_INVALID_STREAM: return "Invalid stream";
		case COMP_SLOT_NOT_ENABLED: return "Slot not enabled";
		case COMP_ENDPOINT_NOT_ENABLED: return "Endpoint not enabled";
		case COMP_SHORT_PACKET: return "Short packet";
		case COMP_RING_UNDERRUN: return "Ring underrun";
		case COMP_RING_OVERRUN: return "Ring overrun";
		case COMP_VF_RING_FULL: return "VF Event Ring Full";
		case COMP_PARAMETER: return "Parameter";
		case COMP_BANDWIDTH_OVERRUN: return "Bandwidth overrun";
		case COMP_CONTEXT_STATE: return "Context state";
		case COMP_NO_PING_RESPONSE: return "No ping response";
		case COMP_EVENT_RING_FULL: return "Event ring full";
		case COMP_INCOMPATIBLE_DEVICE: return "Incompatible device";
		case COMP_MISSED_SERVICE: return "Missed service";
		case COMP_COMMAND_RING_STOPPED: return "Command ring stopped";
		case COMP_COMMAND_ABORTED: return "Command aborted";
		case COMP_STOPPED: return "Stopped";
		case COMP_LENGTH_INVALID: return "Length invalid";
		case COMP_MAX_EXIT_LATENCY: return "Max exit latency too large";
		case COMP_ISOC_OVERRUN: return "Isoch buffer overrun";
		case COMP_EVENT_LOST: return "Event lost";
		case COMP_UNDEFINED: return "Undefined";
		case COMP_INVALID_STREAM_ID: return "Invalid stream ID";
		case COMP_SECONDARY_BANDWIDTH: return "Secondary bandwidth";
		case COMP_SPLIT_TRANSACTION: return "Split transaction";

		default: return "Undefined";
	}
}


void
XHCI::DumpEndpointState(xhci_endpoint_ctx& endpoint)
{
	static const char* states[] = {
		"disabled",
		"running",
		"halted",
		"stopped",
		"error",
		"?"
	};

	static const char* epTypes[] = {
		"notValid",
		"isochOut",
		"bulkOut",
		"interruptOut",
		"control",
		"isochIn",
		"bulkIn",
		"interruptIn",
		"?"
	};

	xhci_endpoint0 dwendpoint0 {.value = _ReadContext(&endpoint.dwendpoint0)};
	xhci_endpoint1 dwendpoint1 {.value = _ReadContext(&endpoint.dwendpoint1)};
	uint64 qwendpoint2 = _ReadContext(&endpoint.qwendpoint2);
	xhci_endpoint4 dwendpoint4 {.value = _ReadContext(&endpoint.dwendpoint4)};

	dprintf(
		"state: %s, "
		"mult: %" B_PRIu32 ", "
		"max_p_streams: %" B_PRIu32 ", "
		"lsa: %" B_PRIu32 ", "
		"interval: %" B_PRIu32 " us, "
		"c_err: %" B_PRIu32 ", "
		"ep_type: %s, "
		"hid: %" B_PRIu32 ", "
		"max_burst: %" B_PRIu32 ", "
		"max_packet_size: %" B_PRIu32 ", "
		"dcs: %d, "
		"tr_dequeue_ptr: %#" B_PRIx64 ", "
		"avg_trb_length: %" B_PRIu32 ", "
		"max_esit_payload: %" B_PRIu32 "\n",

		states[std::min<uint32>(dwendpoint0.state, B_COUNT_OF(states))],
		dwendpoint0.mult + 1,
		dwendpoint0.max_p_streams,
		dwendpoint0.lsa,
		125 * (1 << dwendpoint0.interval),
		dwendpoint1.c_err,
		epTypes[std::min<uint32>(dwendpoint1.ep_type, B_COUNT_OF(epTypes))],
		dwendpoint1.hid,
		dwendpoint1.max_burst + 1,
		dwendpoint1.max_packet_size,
		(qwendpoint2 & ENDPOINT_2_DCS_BIT) != 0,
		qwendpoint2 & ~(uint64)ENDPOINT_2_DCS_BIT,
		dwendpoint4.avg_trb_length,
		dwendpoint4.max_esit_payload_lo + (dwendpoint0.max_esit_payload_hi << 16)
	);
}


void*
XHCI::BusManager::QueryInterface(const char* name)
{
	if (strcmp(name, UsbHostController::ifaceName) == 0)
		return static_cast<UsbHostController*>(&fBase);

	return NULL;
}


status_t
XHCI::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<XHCI> driver(new(std::nothrow) XHCI(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


void
XHCI::SetBusManager(UsbStack* stack, UsbBusManager* busManager)
{
	fStack = stack;
	fBusManager = busManager;
}


status_t
XHCI::Init()
{
	fPciDevice = fNode->QueryBusInterface<PciDevice>();
	fPciDevice->GetPciInfo(&fPCIInfo);

	B_INITIALIZE_SPINLOCK(&fSpinlock);
	mutex_init(&fFinishedLock, "XHCI finished transfers");
	mutex_init(&fEventLock, "XHCI event handler");

	TRACE("constructing new XHCI host controller driver\n");

	// enable busmaster and memory mapped access
	uint16 command = fPciDevice->ReadPciConfig(PCI_command, 2);
	command &= ~(PCI_command_io | PCI_command_int_disable);
	command |= PCI_command_master | PCI_command_memory;

	fPciDevice->WritePciConfig(PCI_command, 2, command);

	// map the registers (low + high for 64-bit when requested)
	phys_addr_t physicalAddress = fPCIInfo.u.h0.base_registers[0];
	if ((fPCIInfo.u.h0.base_register_flags[0] & PCI_address_type)
			== PCI_address_type_64) {
		physicalAddress |= (uint64)fPCIInfo.u.h0.base_registers[1] << 32;
	}

	size_t mapSize = fPCIInfo.u.h0.base_register_sizes[0];

	TRACE("map registers %08" B_PRIxPHYSADDR ", size: %" B_PRIuSIZE "\n",
		physicalAddress, mapSize);

	fRegisterArea = map_physical_memory("XHCI memory mapped registers",
		physicalAddress, mapSize, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void **)&fRegisters);
	if (fRegisterArea < B_OK) {
		TRACE_ERROR("failed to map register memory\n");
		return fRegisterArea;
	}

	// determine the register offsets
	fCapabilityRegisterOffset = 0;
	fOperationalRegisterOffset = HCI_CAPLENGTH(ReadCapReg32(XHCI_HCI_CAPLENGTH));
	fRuntimeRegisterOffset = ReadCapReg32(XHCI_RTSOFF) & ~0x1F;
	fDoorbellRegisterOffset = ReadCapReg32(XHCI_DBOFF) & ~0x3;

	TRACE("mapped registers: %p\n", fRegisters);
	TRACE("operational register offset: %" B_PRId32 "\n", fOperationalRegisterOffset);
	TRACE("runtime register offset: %" B_PRId32 "\n", fRuntimeRegisterOffset);
	TRACE("doorbell register offset: %" B_PRId32 "\n", fDoorbellRegisterOffset);

	int32 interfaceVersion = HCI_VERSION(ReadCapReg32(XHCI_HCI_VERSION));
	if (interfaceVersion < 0x0090 || interfaceVersion > 0x0120) {
		TRACE_ERROR("unsupported interface version: 0x%04" B_PRIx32 "\n",
			interfaceVersion);
		return B_ERROR;
	}
	TRACE_ALWAYS("interface version: 0x%04" B_PRIx32 "\n", interfaceVersion);

	TRACE_ALWAYS("structural parameters: 1:0x%08" B_PRIx32 " 2:0x%08"
		B_PRIx32 " 3:0x%08" B_PRIx32 "\n", ReadCapReg32(XHCI_HCSPARAMS1),
		ReadCapReg32(XHCI_HCSPARAMS2), ReadCapReg32(XHCI_HCSPARAMS3));

	uint32 cparams = ReadCapReg32(XHCI_HCCPARAMS);
	if (cparams == 0xffffffff)
		return B_ERROR;
	TRACE_ALWAYS("capability parameters: 0x%08" B_PRIx32 "\n", cparams);

	// if 64 bytes context structures, then 1
	fContextSizeShift = HCC_CSZ(cparams);

	// Assume ownership of the controller from the BIOS.
	uint32 eec = 0xffffffff;
	uint32 eecp = HCS0_XECP(cparams) << 2;
	for (; eecp != 0 && XECP_NEXT(eec); eecp += XECP_NEXT(eec) << 2) {
		TRACE("eecp register: 0x%08" B_PRIx32 "\n", eecp);

		eec = ReadCapReg32(eecp);
		if (XECP_ID(eec) != XHCI_LEGSUP_CAPID)
			continue;

		if (eec & XHCI_LEGSUP_BIOSOWNED) {
			TRACE_ALWAYS("the host controller is bios owned, claiming"
				" ownership\n");
			WriteCapReg32(eecp, eec | XHCI_LEGSUP_OSOWNED);

			for (int32 i = 0; i < 20; i++) {
				eec = ReadCapReg32(eecp);

				if ((eec & XHCI_LEGSUP_BIOSOWNED) == 0)
					break;

				TRACE_ALWAYS("controller is still bios owned, waiting\n");
				snooze(50000);
			}

			if (eec & XHCI_LEGSUP_BIOSOWNED) {
				TRACE_ERROR("bios won't give up control over the host "
					"controller (ignoring)\n");
			} else if (eec & XHCI_LEGSUP_OSOWNED) {
				TRACE_ALWAYS("successfully took ownership of the host "
					"controller\n");
			}

			// Force off the BIOS owned flag, and clear all SMIs. Some BIOSes
			// do indicate a successful handover but do not remove their SMIs
			// and then freeze the system when interrupts are generated.
			WriteCapReg32(eecp, eec & ~XHCI_LEGSUP_BIOSOWNED);
		}
		break;
	}
	uint32 legctlsts = ReadCapReg32(eecp + XHCI_LEGCTLSTS);
	legctlsts &= XHCI_LEGCTLSTS_DISABLE_SMI;
	legctlsts |= XHCI_LEGCTLSTS_EVENTS_SMI;
	WriteCapReg32(eecp + XHCI_LEGCTLSTS, legctlsts);

	// We need to explicitly take ownership of EHCI ports on earlier Intel chipsets.
	if (fPCIInfo.vendor_id == PCI_VENDOR_INTEL) {
		switch (fPCIInfo.device_id) {
			case PCI_DEVICE_INTEL_PANTHER_POINT_XHCI:
			case PCI_DEVICE_INTEL_LYNX_POINT_XHCI:
			case PCI_DEVICE_INTEL_LYNX_POINT_LP_XHCI:
			case PCI_DEVICE_INTEL_BAYTRAIL_XHCI:
			case PCI_DEVICE_INTEL_WILDCAT_POINT_XHCI:
			case PCI_DEVICE_INTEL_WILDCAT_POINT_LP_XHCI:
				_SwitchIntelPorts();
				break;
		}
	}

	// halt the host controller
	if (ControllerHalt() < B_OK) {
		return B_ERROR;
	}

	// reset the host controller
	if (ControllerReset() < B_OK) {
		TRACE_ERROR("host controller failed to reset\n");
		return B_ERROR;
	}

	fCmdCompSem = create_sem(0, "XHCI Command Complete");
	fFinishTransfersSem = create_sem(0, "XHCI Finish Transfers");
	fEventSem = create_sem(0, "XHCI Event");
	if (fFinishTransfersSem < B_OK || fCmdCompSem < B_OK || fEventSem < B_OK) {
		TRACE_ERROR("failed to create semaphores\n");
		return B_ERROR;
	}

	// create event handler thread
	fEventThread = spawn_kernel_thread(EventThread, "xhci event thread",
		B_URGENT_PRIORITY, (void *)this);
	resume_thread(fEventThread);

	// create finisher service thread
	fFinishThread = spawn_kernel_thread(FinishThread, "xhci finish thread",
		B_URGENT_PRIORITY - 1, (void *)this);
	resume_thread(fFinishThread);

	// Find the right interrupt vector, using MSIs if available.
	fIRQ = fPCIInfo.u.h0.interrupt_line;
#if 0
	if (fPciDevice->GetMsixCount() >= 1) {
		uint8 msiVector = 0;
		if (fPciDevice->ConfigureMsix(1, &msiVector) == B_OK
			&& fPciDevice->EnableMsix() == B_OK) {
			TRACE_ALWAYS("using MSI-X\n");
			fIRQ = msiVector;
			fUseMSI = true;
		}
	} else
#endif
	if (fPciDevice->GetMsiCount() >= 1) {
		uint8 msiVector = 0;
		if (fPciDevice->ConfigureMsi(1, &msiVector) == B_OK
			&& fPciDevice->EnableMsi() == B_OK) {
			TRACE_ALWAYS("using message signaled interrupts\n");
			fIRQ = msiVector;
			fUseMSI = true;
		}
	}

	if (fIRQ == 0 || fIRQ == 0xFF) {
		TRACE_MODULE_ERROR("device PCI:%d:%d:%d was assigned an invalid IRQ\n",
			fPCIInfo.bus, fPCIInfo.device, fPCIInfo.function);
		return B_ERROR;
	}

	// Install the interrupt handler
	TRACE("installing interrupt handler, irq: %" B_PRIu8 "\n", fIRQ);
	install_io_interrupt_handler(fIRQ, InterruptHandler, (void *)this, 0);

	static const device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "USB Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/usb/driver/v1"}},
		{}
	};
	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fBusManagerDriver), attrs, NULL));

	TRACE("driver construction successful\n");
	return B_OK;
}


XHCI::~XHCI()
{
	TRACE("tear down XHCI host controller driver\n");

	WriteOpReg(XHCI_CMD, 0);

	int32 result = 0;
	fStopThreads = true;
	delete_sem(fCmdCompSem);
	delete_sem(fFinishTransfersSem);
	delete_sem(fEventSem);
	wait_for_thread(fFinishThread, &result);
	wait_for_thread(fEventThread, &result);

	mutex_destroy(&fFinishedLock);
	mutex_destroy(&fEventLock);

	remove_io_interrupt_handler(fIRQ, InterruptHandler, (void *)this);

	delete_area(fRegisterArea);
	delete_area(fErstArea);
	for (uint32 i = 0; i < fScratchpadCount; i++)
		delete_area(fScratchpadArea[i]);
	delete_area(fDcbaArea);

	if (fUseMSI) {
		fPciDevice->DisableMsi();
		fPciDevice->UnconfigureMsi();
	}
}


void
XHCI::_SwitchIntelPorts()
{
	TRACE("Looking for EHCI owned ports\n");
	uint32 ports = fPciDevice->ReadPciConfig(XHCI_INTEL_USB3PRM, 4);
	TRACE("Superspeed Ports: 0x%" B_PRIx32 "\n", ports);
	fPciDevice->WritePciConfig(XHCI_INTEL_USB3_PSSEN, 4, ports);
	ports = fPciDevice->ReadPciConfig(XHCI_INTEL_USB3_PSSEN, 4);
	TRACE("Superspeed ports now under XHCI : 0x%" B_PRIx32 "\n", ports);
	ports = fPciDevice->ReadPciConfig(XHCI_INTEL_USB2PRM, 4);
	TRACE("USB 2.0 Ports : 0x%" B_PRIx32 "\n", ports);
	fPciDevice->WritePciConfig(XHCI_INTEL_XUSB2PR, 4, ports);
	ports = fPciDevice->ReadPciConfig(XHCI_INTEL_XUSB2PR, 4);
	TRACE("USB 2.0 ports now under XHCI: 0x%" B_PRIx32 "\n", ports);
}


status_t
XHCI::Start()
{
	TRACE_ALWAYS("starting XHCI host controller\n");
	TRACE("usbcmd: 0x%08" B_PRIx32 "; usbsts: 0x%08" B_PRIx32 "\n",
		ReadOpReg(XHCI_CMD), ReadOpReg(XHCI_STS));

	if (WaitOpBits(XHCI_STS, STS_CNR, 0) != B_OK) {
		TRACE("Start() failed STS_CNR\n");
	}

	if ((ReadOpReg(XHCI_CMD) & CMD_RUN) != 0) {
		TRACE_ERROR("Start() warning, starting running XHCI controller!\n");
	}

	if ((ReadOpReg(XHCI_PAGESIZE) & (1 << 0)) == 0) {
		TRACE_ERROR("controller does not support 4K page size\n");
		return B_ERROR;
	}

	// read port count from capability register
	uint32 capabilities = ReadCapReg32(XHCI_HCSPARAMS1);
	fPortCount = HCS_MAX_PORTS(capabilities);
	if (fPortCount == 0) {
		TRACE_ERROR("invalid number of ports: %u\n", fPortCount);
		return B_ERROR;
	}

	fSlotCount = HCS_MAX_SLOTS(capabilities);
	if (fSlotCount > XHCI_MAX_DEVICES)
		fSlotCount = XHCI_MAX_DEVICES;
	WriteOpReg(XHCI_CONFIG, fSlotCount);

	// find out which protocol is used for each port
	uint8 portFound = 0;
	uint32 cparams = ReadCapReg32(XHCI_HCCPARAMS);
	uint32 eec = 0xffffffff;
	uint32 eecp = HCS0_XECP(cparams) << 2;
	for (; eecp != 0 && XECP_NEXT(eec) && portFound < fPortCount;
		eecp += XECP_NEXT(eec) << 2) {
		eec = ReadCapReg32(eecp);
		if (XECP_ID(eec) != XHCI_SUPPORTED_PROTOCOLS_CAPID)
			continue;
		if (XHCI_SUPPORTED_PROTOCOLS_0_MAJOR(eec) > 3)
			continue;
		uint32 temp = ReadCapReg32(eecp + 8);
		uint32 offset = XHCI_SUPPORTED_PROTOCOLS_1_OFFSET(temp);
		uint32 count = XHCI_SUPPORTED_PROTOCOLS_1_COUNT(temp);
		if (offset == 0 || count == 0)
			continue;
		offset--;
		for (uint32 i = offset; i < offset + count; i++) {
			if (XHCI_SUPPORTED_PROTOCOLS_0_MAJOR(eec) == 0x3) {
				fRootHubPorts[i] = fRootHub3.AddPort(i);
				fPortSpeeds[i] = USB_SPEED_SUPERSPEED;
			} else {
				fRootHubPorts[i] = fRootHub2.AddPort(i);
				fPortSpeeds[i] = USB_SPEED_HIGHSPEED;
			}

			TRACE_ALWAYS("speed for port %" B_PRId32 " is %s\n", i,
				fPortSpeeds[i] == USB_SPEED_SUPERSPEED ? "super" : "high");
		}
		portFound += count;
	}

	uint32 params2 = ReadCapReg32(XHCI_HCSPARAMS2);
	fScratchpadCount = HCS_MAX_SC_BUFFERS(params2);
	if (fScratchpadCount > XHCI_MAX_SCRATCHPADS) {
		TRACE_ERROR("invalid number of scratchpads: %" B_PRIu32 "\n",
			fScratchpadCount);
		return B_ERROR;
	}

	uint32 params3 = ReadCapReg32(XHCI_HCSPARAMS3);
	fExitLatMax = HCS_U1_DEVICE_LATENCY(params3)
		+ HCS_U2_DEVICE_LATENCY(params3);

	// clear interrupts & disable device notifications
	WriteOpReg(XHCI_STS, ReadOpReg(XHCI_STS));
	WriteOpReg(XHCI_DNCTRL, 0);

	// allocate Device Context Base Address array
	phys_addr_t dmaAddress;
	fDcbaArea = fStack->AllocateArea((void **)&fDcba, &dmaAddress,
		sizeof(*fDcba), "DCBA Area");
	if (fDcbaArea < B_OK) {
		TRACE_ERROR("unable to create the DCBA area\n");
		return B_ERROR;
	}
	memset(fDcba, 0, sizeof(*fDcba));
	memset(fScratchpadArea, 0, sizeof(fScratchpadArea));
	memset(fScratchpad, 0, sizeof(fScratchpad));

	// setting the first address to the scratchpad array address
	fDcba->baseAddress[0] = dmaAddress
		+ offsetof(struct xhci_device_context_array, scratchpad);

	// fill up the scratchpad array with scratchpad pages
	for (uint32 i = 0; i < fScratchpadCount; i++) {
		phys_addr_t scratchDmaAddress;
		fScratchpadArea[i] = fStack->AllocateArea((void **)&fScratchpad[i],
			&scratchDmaAddress, B_PAGE_SIZE, "Scratchpad Area");
		if (fScratchpadArea[i] < B_OK) {
			TRACE_ERROR("unable to create the scratchpad area\n");
			return B_ERROR;
		}
		fDcba->scratchpad[i] = scratchDmaAddress;
	}

	TRACE("setting DCBAAP %" B_PRIxPHYSADDR "\n", dmaAddress);
	WriteOpReg(XHCI_DCBAAP_LO, (uint32)dmaAddress);
	WriteOpReg(XHCI_DCBAAP_HI, (uint32)(dmaAddress >> 32));

	// allocate Event Ring Segment Table
/*
	Virt        Phys                       Size
	fErst       XHCI_ERSTBA                sizeof(xhci_erst_element)
	fEventRing  XHCI_ERDP, fErst->rs_addr  XHCI_MAX_EVENTS * sizeof(xhci_trb)
	fCmdRing    XHCI_CRCR                  XHCI_MAX_COMMANDS * sizeof(xhci_trb)
*/
	uint8 *addr;
	fErstArea = fStack->AllocateArea((void **)&addr, &dmaAddress,
		(XHCI_MAX_COMMANDS + XHCI_MAX_EVENTS) * sizeof(xhci_trb)
		+ sizeof(xhci_erst_element),
		"USB XHCI ERST CMD_RING and EVENT_RING Area");

	if (fErstArea < B_OK) {
		TRACE_ERROR("unable to create the ERST AND RING area\n");
		delete_area(fDcbaArea);
		return B_ERROR;
	}
	fErst = (xhci_erst_element *)addr;
	memset(fErst, 0, (XHCI_MAX_COMMANDS + XHCI_MAX_EVENTS) * sizeof(xhci_trb)
		+ sizeof(xhci_erst_element));

	// fill with Event Ring Segment Base Address and Event Ring Segment Size
	fErst->rs_addr = dmaAddress + sizeof(xhci_erst_element);
	fErst->rs_size = XHCI_MAX_EVENTS;
	fErst->rsvdz = 0;

	addr += sizeof(xhci_erst_element);
	fEventRing = (xhci_trb *)addr;
	addr += XHCI_MAX_EVENTS * sizeof(xhci_trb);
	fCmdRing = (xhci_trb *)addr;

	TRACE("setting ERST size\n");
	WriteRunReg32(XHCI_ERSTSZ(0), XHCI_ERSTS_SET(1));

	TRACE("setting ERDP addr = 0x%" B_PRIx64 "\n", fErst->rs_addr);
	WriteRunReg32(XHCI_ERDP_LO(0), (uint32)fErst->rs_addr);
	WriteRunReg32(XHCI_ERDP_HI(0), (uint32)(fErst->rs_addr >> 32));

	TRACE("setting ERST base addr = 0x%" B_PRIxPHYSADDR "\n", dmaAddress);
	WriteRunReg32(XHCI_ERSTBA_LO(0), (uint32)dmaAddress);
	WriteRunReg32(XHCI_ERSTBA_HI(0), (uint32)(dmaAddress >> 32));

	dmaAddress += sizeof(xhci_erst_element) + XHCI_MAX_EVENTS
		* sizeof(xhci_trb);

	// Make sure the Command Ring is stopped
	if ((ReadOpReg(XHCI_CRCR_LO) & CRCR_CRR) != 0) {
		TRACE_ALWAYS("Command Ring is running, send stop/cancel\n");
		WriteOpReg(XHCI_CRCR_LO, CRCR_CS);
		WriteOpReg(XHCI_CRCR_HI, 0);
		WriteOpReg(XHCI_CRCR_LO, CRCR_CA);
		WriteOpReg(XHCI_CRCR_HI, 0);
		snooze(1000);
		if ((ReadOpReg(XHCI_CRCR_LO) & CRCR_CRR) != 0) {
			TRACE_ERROR("Command Ring still running after stop/cancel\n");
		}
	}
	TRACE("setting CRCR addr = 0x%" B_PRIxPHYSADDR "\n", dmaAddress);
	WriteOpReg(XHCI_CRCR_LO, (uint32)dmaAddress | CRCR_RCS);
	WriteOpReg(XHCI_CRCR_HI, (uint32)(dmaAddress >> 32));
	// link trb
	fCmdRing[XHCI_MAX_COMMANDS - 1].address = dmaAddress;

	TRACE("setting interrupt rate\n");

	// Setting IMOD below 0x3F8 on Intel Lynx Point can cause IRQ lockups
	if (fPCIInfo.vendor_id == PCI_VENDOR_INTEL
		&& (fPCIInfo.device_id == PCI_DEVICE_INTEL_PANTHER_POINT_XHCI
			|| fPCIInfo.device_id == PCI_DEVICE_INTEL_LYNX_POINT_XHCI
			|| fPCIInfo.device_id == PCI_DEVICE_INTEL_LYNX_POINT_LP_XHCI
			|| fPCIInfo.device_id == PCI_DEVICE_INTEL_BAYTRAIL_XHCI
			|| fPCIInfo.device_id == PCI_DEVICE_INTEL_WILDCAT_POINT_XHCI)) {
		WriteRunReg32(XHCI_IMOD(0), 0x000003f8); // 4000 irq/s
	} else {
		WriteRunReg32(XHCI_IMOD(0), 0x000001f4); // 8000 irq/s
	}

	TRACE("enabling interrupt\n");
	WriteRunReg32(XHCI_IMAN(0), ReadRunReg32(XHCI_IMAN(0)) | IMAN_INTR_ENA);

	WriteOpReg(XHCI_CMD, CMD_RUN | CMD_INTE | CMD_HSEE);

	// wait for start up state
	if (WaitOpBits(XHCI_STS, STS_HCH, 0) != B_OK) {
		TRACE_ERROR("HCH start up timeout\n");
	}

	CHECK_RET(fRootHub2.Init(fBusManager));
	CHECK_RET(fRootHub3.Init(fBusManager));

	TRACE_ALWAYS("successfully started the controller\n");

#ifdef TRACE_USB
	TRACE("No-Op test...\n");
	Noop();
#endif

	return B_OK;
}


status_t
XHCI::Stop()
{
	// TODO
	return B_OK;
}


status_t
XHCI::SubmitTransfer(UsbBusTransfer* transfer)
{
	TRACE("SubmitTransfer(%p)\n", transfer);

	UsbBusPipe *pipe = transfer->TransferPipe();

	// short circuit the root hub
	if (pipe->GetDevice() == fRootHub2.GetDevice())
		return fRootHub2.ProcessTransfer(transfer);
	if (pipe->GetDevice() == fRootHub3.GetDevice())
		return fRootHub3.ProcessTransfer(transfer);

	if (pipe->Type() == USB_PIPE_CONTROL)
		return SubmitControlRequest(transfer);

	return SubmitNormalRequest(transfer);
}


status_t
XHCI::SubmitControlRequest(UsbBusTransfer *transfer)
{
	UsbBusPipe *pipe = transfer->TransferPipe();
	usb_request_data *requestData = transfer->RequestData();
	bool directionIn = (requestData->RequestType & USB_REQTYPE_DEVICE_IN) != 0;

	TRACE("SubmitControlRequest() length %d\n", requestData->Length);

	XhciEndpoint *endpoint = (XhciEndpoint *)pipe->ControllerCookie();
	if (endpoint == NULL) {
		TRACE_ERROR("control pipe has no endpoint!\n");
		return B_BAD_VALUE;
	}
	if (endpoint->fDevice == NULL) {
		panic("endpoint is not initialized!");
		return B_NO_INIT;
	}

	status_t status = transfer->InitKernelAccess();
	if (status != B_OK)
		return status;

	XhciTransferDesc *descriptor = CreateDescriptor(3, 1, requestData->Length);
	if (descriptor == NULL)
		return B_NO_MEMORY;
	descriptor->fTransfer = transfer;

	// Setup Stage
	uint8 index = 0;
	memcpy(&descriptor->fTrbs[index].address, requestData,
		sizeof(usb_request_data));
	descriptor->fTrbs[index].status = TRB_2_IRQ(0) | TRB_2_BYTES(8);
	descriptor->fTrbs[index].flags
		= TRB_3_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_3_IDT_BIT | TRB_3_CYCLE_BIT;
	if (requestData->Length > 0) {
		descriptor->fTrbs[index].flags |=
			directionIn ? TRB_3_TRT_IN : TRB_3_TRT_OUT;
	}

	index++;

	// Data Stage (if any)
	if (requestData->Length > 0) {
		descriptor->fTrbs[index].address = descriptor->fBufferAddrs[0];
		descriptor->fTrbs[index].status = TRB_2_IRQ(0)
			| TRB_2_BYTES(requestData->Length)
			| TRB_2_TD_SIZE(0);
		descriptor->fTrbs[index].flags = TRB_3_TYPE(TRB_TYPE_DATA_STAGE)
				| (directionIn ? TRB_3_DIR_IN : 0)
				| TRB_3_CYCLE_BIT;

		if (!directionIn) {
			transfer->PrepareKernelAccess();
			descriptor->Write(transfer->Vector(),
				transfer->VectorCount(), transfer->IsPhysical());
		}

		index++;
	}

	// Status Stage
	descriptor->fTrbs[index].address = 0;
	descriptor->fTrbs[index].status = TRB_2_IRQ(0);
	descriptor->fTrbs[index].flags = TRB_3_TYPE(TRB_TYPE_STATUS_STAGE)
			| TRB_3_CHAIN_BIT | TRB_3_ENT_BIT | TRB_3_CYCLE_BIT;
		// The CHAIN bit must be set when using an Event Data TRB
		// (XHCI 1.2 § 6.4.1.2.3 Table 6-31 p472).

	// Status Stage is an OUT transfer when the device is sending data
	// (XHCI 1.2 § 4.11.2.2 Table 4-7 p213), otherwise set the IN bit.
	if (requestData->Length == 0 || !directionIn)
		descriptor->fTrbs[index].flags |= TRB_3_DIR_IN;

	descriptor->fTrbUsed = index + 1;

	status = endpoint->LinkDescriptor(descriptor);
	if (status != B_OK) {
		delete descriptor;
		return status;
	}

	return B_OK;
}


status_t
XHCI::SubmitNormalRequest(UsbBusTransfer *transfer)
{
	TRACE("SubmitNormalRequest() length %" B_PRIuSIZE "\n", transfer->FragmentLength());

	UsbBusPipe *pipe = transfer->TransferPipe();
	usb_isochronous_data *isochronousData = transfer->IsochronousData();
	bool directionIn = (pipe->Direction() == UsbBusPipe::In);

	XhciEndpoint *endpoint = (XhciEndpoint *)pipe->ControllerCookie();
	if (endpoint == NULL) {
		TRACE_ERROR("pipe has no endpoint!\n");
		return B_BAD_VALUE;
	}
	if (endpoint->fDevice == NULL) {
		panic("endpoint is not initialized!");
		return B_NO_INIT;
	}

	status_t status = transfer->InitKernelAccess();
	if (status != B_OK)
		return status;

	// TRBs within a TD must be "grouped" into TD Fragments, which mostly means
	// that a max_burst_payload boundary cannot be crossed within a TRB, but
	// only between TRBs. More than one TRB can be in a TD Fragment, but we keep
	// things simple by setting trbSize to the MBP. (XHCI 1.2 § 4.11.7.1 p235.)
	size_t trbSize = endpoint->fMaxBurstPayload;

	if (isochronousData != NULL) {
		if (isochronousData->packet_count == 0)
			return B_BAD_VALUE;

		// Isochronous transfers use more specifically sized packets.
		trbSize = transfer->DataLength() / isochronousData->packet_count;
		if (trbSize == 0 || trbSize > pipe->MaxPacketSize() || trbSize
				!= (size_t)isochronousData->packet_descriptors[0].request_length)
			return B_BAD_VALUE;
	}

	// Now that we know trbSize, compute the count.
	const int32 trbCount = (transfer->FragmentLength() + trbSize - 1) / trbSize;

	XhciTransferDesc *td = CreateDescriptor(trbCount, trbCount, trbSize);
	if (td == NULL)
		return B_NO_MEMORY;

	// Normal Stage
	const size_t maxPacketSize = pipe->MaxPacketSize();
	size_t remaining = transfer->FragmentLength();
	for (int32 i = 0; i < trbCount; i++) {
		int32 trbLength = (remaining < trbSize) ? remaining : trbSize;
		remaining -= trbLength;

		// The "TD Size" field of a transfer TRB indicates the number of
		// remaining maximum-size *packets* in this TD, *not* including the
		// packets in the current TRB, and capped at 31 if there are more
		// than 31 packets remaining in the TD. (XHCI 1.2 § 4.11.2.4 p218.)
		int32 tdSize = (remaining + maxPacketSize - 1) / maxPacketSize;
		if (tdSize > 31)
			tdSize = 31;

		td->fTrbs[i].address = td->fBufferAddrs[i];
		td->fTrbs[i].status = xhci_trb_status{
			.transfer_length = (uint32)trbLength,
			.td_size = (uint32)tdSize,
			.irq_target = 0
		}.value;
		td->fTrbs[i].flags = xhci_trb_flags{
			.cycle = true,
			.chain = true,
			.trb_type = TRB_TYPE_NORMAL
		}.value;

		td->fTrbUsed++;
	}

	// Isochronous-specific
	if (isochronousData != NULL) {
		// This is an isochronous transfer; we need to make the first TRB
		// an isochronous TRB.
		td->fTrbs[0].flags &= ~(TRB_3_TYPE(TRB_TYPE_NORMAL));
		td->fTrbs[0].flags |= TRB_3_TYPE(TRB_TYPE_ISOCH);

		// Isochronous pipes are scheduled by microframes, one of which
		// is 125us for USB 2 and above. But for USB 1 it was 1ms, so
		// we need to use a different frame delta for that case.
		uint8 frameDelta = 1;
		if (transfer->TransferPipe()->Speed() == USB_SPEED_FULLSPEED)
			frameDelta = 8;

		// TODO: We do not currently take Mult into account at all!
		// How are we supposed to do that here?

		// Determine the (starting) frame number: if ISO_ASAP is set,
		// we are queueing this "right away", and so want to reset
		// the starting_frame_number. Otherwise we use the passed one.
		uint32 frame;
		if ((isochronousData->flags & USB_ISO_ASAP) != 0
				|| isochronousData->starting_frame_number == NULL) {
			// All reads from the microframe index register must be
			// incremented by 1. (XHCI 1.2 § 4.14.2.1.4 p265.)
			frame = ReadRunReg32(XHCI_MFINDEX) + 1;
			td->fTrbs[0].flags |= TRB_3_ISO_SIA_BIT;
		} else {
			frame = *isochronousData->starting_frame_number;
			td->fTrbs[0].flags |= TRB_3_FRID(frame);
		}
		frame = (frame + frameDelta) % 2048;
		if (isochronousData->starting_frame_number != NULL)
			*isochronousData->starting_frame_number = frame;

		// TODO: The OHCI bus driver seems to also do this for inbound
		// isochronous transfers. Perhaps it should be moved into the stack?
		if (directionIn) {
			for (uint32 i = 0; i < isochronousData->packet_count; i++) {
				isochronousData->packet_descriptors[i].actual_length = 0;
				isochronousData->packet_descriptors[i].status = B_NO_INIT;
			}
		}
	}

	// Set the ENT (Evaluate Next TRB) bit, so that the HC will not switch
	// contexts before evaluating the Link TRB that _LinkDescriptorForPipe
	// will insert, as otherwise there would be a race between us freeing
	// and unlinking the descriptor, and the controller evaluating the Link TRB
	// and thus getting back onto the main ring and executing the Event Data
	// TRB that generates the interrupt for this transfer.
	//
	// Note that we *do not* unset the CHAIN bit in this TRB, thus including
	// the Link TRB in this TD formally, which is required when using the
	// ENT bit. (XHCI 1.2 § 4.12.3 p250.)
	td->fTrbs[td->fTrbUsed - 1].flags |= TRB_3_ENT_BIT;

	if (!directionIn) {
		TRACE("copying out iov count %ld\n", transfer->VectorCount());
		status_t status = transfer->PrepareKernelAccess();
		if (status != B_OK) {
			delete td;
			return status;
		}
		td->Write(transfer->Vector(),
			transfer->VectorCount(), transfer->IsPhysical());
	}

	td->fTransfer = transfer;
	status = endpoint->LinkDescriptor(td);
	if (status != B_OK) {
		delete td;
		return status;
	}

	return B_OK;
}


status_t
XHCI::CancelQueuedTransfers(UsbBusPipe* pipe, bool force)
{
	XhciEndpoint* endpoint = (XhciEndpoint*)pipe->ControllerCookie();
	if (endpoint == NULL || endpoint->fTrbs == NULL) {
		// Someone's de-allocated this pipe or endpoint in the meantime.
		// (Possibly AllocateDevice failed, and we were the temporary pipe.)
		return B_NO_INIT;
	}

#ifndef TRACE_USB
	if (force)
#endif
	{
		TRACE_ALWAYS("cancel queued transfers (%" B_PRId8 ") for pipe %p (%d)\n",
			endpoint->fUsed, pipe, pipe->EndpointAddress());
	}

	MutexLocker endpointLocker(endpoint->fLock);

	if (endpoint->fTransferDescs.IsEmpty()) {
		// There aren't any currently pending transfers to cancel.
		return B_OK;
	}

	// Calling the callbacks while holding the endpoint lock could potentially
	// cause deadlocks, so we instead store them in a pointer array. We need
	// to do this separately from freeing the TDs, for in the case we fail
	// to stop the endpoint, we cancel the transfers but do not free the TDs.
	UsbBusTransfer* transfers[XHCI_MAX_TRANSFERS];
	int32 transfersCount = 0;

	for (XhciTransferDesc* td = endpoint->fTransferDescs.First(); td != NULL; td = endpoint->fTransferDescs.GetNext(td)) {
		if (td->fTransfer == NULL)
			continue;

		// We can't cancel or delete transfers under "force", as they probably
		// are not safe to use anymore.
		if (!force) {
			transfers[transfersCount] = td->fTransfer;
			transfersCount++;
		}
		td->fTransfer = NULL;
	}

	// It is possible that while waiting for the stop-endpoint command to
	// complete, one of the queued transfers posts a completion event, so in
	// order to avoid a deadlock, we must unlock the endpoint.
	endpointLocker.Unlock();
	status_t status = StopEndpoint(false, endpoint);
	if (status != B_OK && status != B_DEV_STALLED) {
		// It is possible that the endpoint was stopped by the controller at the
		// same time our STOP command was in progress, causing a "Context State"
		// error. In that case, try again; if the endpoint is already stopped,
		// StopEndpoint will notice this. (XHCI 1.2 § 4.6.9 p137.)
		status = StopEndpoint(false, endpoint);
	}
	if (status == B_DEV_STALLED) {
		// Only exit from a Halted state is a RESET. (XHCI 1.2 § 4.8.3 p163.)
		TRACE_ERROR("cancel queued transfers: halted endpoint, reset!\n");
		status = ResetEndpoint(false, endpoint);
	}
	endpointLocker.Lock();

	// Detach the head TD from the endpoint.
	XhciTransferDesc::List tdList;
	tdList.MoveFrom(&endpoint->fTransferDescs);

	if (status == B_OK) {
		// Clear the endpoint's TRBs.
		memset(endpoint->fTrbs, 0, sizeof(xhci_trb) * XHCI_ENDPOINT_RING_SIZE);
		endpoint->fUsed = 0;
		endpoint->fCurrent = 0;

		// Set dequeue pointer location to the beginning of the ring.
		SetTRDequeue(endpoint->fTrbAddr, 0, endpoint->fId + 1,
			endpoint->fDevice->fSlot);

		// We don't need to do anything else to restart the ring, as it will resume
		// operation as normal upon the next doorbell. (XHCI 1.2 § 4.6.9 p136.)
	} else {
		// We couldn't stop the endpoint. Most likely the device has been
		// removed and the endpoint was stopped by the hardware, or is
		// for some reason busy and cannot be stopped.
		TRACE_ERROR("cancel queued transfers: could not stop endpoint: %s!\n",
			strerror(status));

		// Instead of freeing the TDs, we want to leave them in the endpoint
		// so that when/if the hardware returns, they can be properly unlinked,
		// as otherwise the endpoint could get "stuck" by having the "used"
		// slowly accumulate due to "dead" transfers.
		endpoint->fTransferDescs.MoveFrom(&tdList);
	}

	endpointLocker.Unlock();

	for (int32 i = 0; i < transfersCount; i++) {
		transfers[i]->Finished(B_CANCELED, 0);
		transfers[i]->Free();
	}

	// This loop looks a bit strange because we need to store the "next"
	// pointer before freeing the descriptor.
	XhciTransferDesc* td;
	while ((td = tdList.RemoveHead()) != NULL)
		delete td;

	return B_OK;
}


status_t
XHCI::StartDebugTransfer(UsbBusTransfer* transfer)
{
	UsbBusPipe *pipe = transfer->TransferPipe();
	XhciEndpoint *endpoint = (XhciEndpoint *)pipe->ControllerCookie();
	if (endpoint == NULL)
		return B_BAD_VALUE;

	// Check all locks that we are going to hit when running transfers.
	if (mutex_trylock(&endpoint->fLock) != B_OK)
		return B_WOULD_BLOCK;
	if (mutex_trylock(&fFinishedLock) != B_OK) {
		mutex_unlock(&endpoint->fLock);
		return B_WOULD_BLOCK;
	}
	if (mutex_trylock(&fEventLock) != B_OK) {
		mutex_unlock(&endpoint->fLock);
		mutex_unlock(&fFinishedLock);
		return B_WOULD_BLOCK;
	}
	mutex_unlock(&endpoint->fLock);
	mutex_unlock(&fFinishedLock);
	mutex_unlock(&fEventLock);

	status_t status = SubmitTransfer(transfer);
	if (status != B_OK)
		return status;

	// The endpoint's head TD is the TD of the just-submitted transfer.
	// Just like EHCI, abuse the callback cookie to hold the TD pointer.
	transfer->SetCallback(NULL, endpoint->fTransferDescs.First());

	return B_OK;
}


status_t
XHCI::CheckDebugTransfer(UsbBusTransfer* transfer)
{
	XhciTransferDesc *transfer_td = (XhciTransferDesc *)transfer->CallbackCookie();
	if (transfer_td == NULL)
		return B_NO_INIT;

	// Process events once, and then look for it in the finished list.
	ProcessEvents();
	for (XhciTransferDesc *td = fFinishedList.First(); td != NULL; td = fFinishedList.GetNext(td)) {
		if (td != transfer_td)
			continue;

		// We've found it!
		fFinishedList.Remove(td);

		bool directionIn = (transfer->TransferPipe()->Direction() != UsbBusPipe::Out);
		status_t status = (td->fTrbCompletionCode == COMP_SUCCESS
			|| td->fTrbCompletionCode == COMP_SHORT_PACKET) ? B_OK : B_ERROR;

		if (status == B_OK && directionIn) {
			td->Read(transfer->Vector(), transfer->VectorCount(),
				transfer->IsPhysical());
		}

		delete td;
		transfer->SetCallback(NULL, NULL);
		return status;
	}

	// We didn't find it.
	spin(75);
	return B_DEV_PENDING;
}


void
XHCI::CancelDebugTransfer(UsbBusTransfer* transfer)
{
	while (CheckDebugTransfer(transfer) == B_DEV_PENDING)
		spin(100);
}


status_t
XHCI::NotifyPipeChange(UsbBusPipe* pipe, usb_change change)
{
	TRACE("pipe change %d for pipe %p (%d)\n", change, pipe,
		pipe->EndpointAddress());

	switch (change) {
	case USB_CHANGE_CREATED:
		return _InsertEndpointForPipe(pipe);
	case USB_CHANGE_DESTROYED:
		return _RemoveEndpointForPipe(pipe);

	case USB_CHANGE_PIPE_POLICY_CHANGED:
		// We don't care about these, at least for now.
		return B_OK;
	}

	TRACE_ERROR("unknown pipe change!\n");
	return B_UNSUPPORTED;
}


XhciTransferDesc *
XHCI::CreateDescriptor(uint32 trbCount, uint32 bufferCount, size_t bufferSize)
{
	XhciTransferDesc *result = new(std::nothrow) XhciTransferDesc(fStack);

	if (result == NULL) {
		TRACE_ERROR("failed to allocate a transfer descriptor\n");
		return NULL;
	}

	// We always allocate 1 more TRB than requested, so that
	// _LinkDescriptorForPipe() has room to insert a link TRB.
	trbCount++;
	if (fStack->AllocateChunk((void **)&result->fTrbs, &result->fTrbAddr,
			(trbCount * sizeof(xhci_trb))) < B_OK) {
		TRACE_ERROR("failed to allocate TRBs\n");
		delete result;
		return NULL;
	}
	result->fTrbCount = trbCount;

	if (bufferSize > 0) {
		// Due to how the USB stack allocates physical memory, we can't just
		// request one large chunk the size of the transfer, and so instead we
		// create a series of buffers as requested by our caller.

		// We store the buffer pointers and addresses in one memory block.
		result->fBuffers = (void**)calloc(bufferCount,
			(sizeof(void*) + sizeof(phys_addr_t)));
		if (result->fBuffers == NULL) {
			TRACE_ERROR("unable to allocate space for buffer infos\n");
			delete result;
			return NULL;
		}
		result->fBufferAddrs = (phys_addr_t*)&result->fBuffers[bufferCount];
		result->fBufferSize = bufferSize;
		result->fBufferCount = bufferCount;

		// Optimization: If the requested total size of all buffers is less
		// than 32*B_PAGE_SIZE (the maximum size that the physical memory
		// allocator can handle), we allocate only one buffer and segment it.
		size_t totalSize = bufferSize * bufferCount;
		if (totalSize < (32 * B_PAGE_SIZE)) {
			if (fStack->AllocateChunk(&result->fBuffers[0],
					&result->fBufferAddrs[0], totalSize) < B_OK) {
				TRACE_ERROR("unable to allocate space for large buffer (size %ld)\n",
					totalSize);
				delete result;
				return NULL;
			}
			for (uint32 i = 1; i < bufferCount; i++) {
				result->fBuffers[i] = (void*)((addr_t)(result->fBuffers[i - 1])
					+ bufferSize);
				result->fBufferAddrs[i] = result->fBufferAddrs[i - 1]
					+ bufferSize;
			}
		} else {
			// Otherwise, we allocate each buffer individually.
			for (uint32 i = 0; i < bufferCount; i++) {
				if (fStack->AllocateChunk(&result->fBuffers[i],
						&result->fBufferAddrs[i], bufferSize) < B_OK) {
					TRACE_ERROR("unable to allocate space for a buffer (size "
						"%" B_PRIuSIZE ", count %" B_PRIu32 ")\n",
						bufferSize, bufferCount);
					delete result;
					return NULL;
				}
			}
		}
	} else {
		result->fBuffers = NULL;
		result->fBufferAddrs = NULL;
	}

	TRACE("CreateDescriptor allocated %p, buffer_size %ld, buffer_count %" B_PRIu32 "\n",
		result, result->fBufferSize, result->fBufferCount);

	return result;
}


XhciTransferDesc::~XhciTransferDesc()
{
	if (fTrbs != NULL) {
		fStack->FreeChunk(fTrbs, fTrbAddr,
			(fTrbCount * sizeof(xhci_trb)));
	}
	if (fBuffers != NULL) {
		size_t totalSize = fBufferSize * fBufferCount;
		if (totalSize < (32 * B_PAGE_SIZE)) {
			// This was allocated as one contiguous buffer.
			fStack->FreeChunk(fBuffers[0], fBufferAddrs[0],
				totalSize);
		} else {
			for (uint32 i = 0; i < fBufferCount; i++) {
				if (fBuffers[i] == NULL)
					continue;
				fStack->FreeChunk(fBuffers[i], fBufferAddrs[i],
					fBufferSize);
			}
		}

		free(fBuffers);
	}
}


size_t
XhciTransferDesc::Write(generic_io_vec *vector, size_t vectorCount, bool physical)
{
	size_t written = 0;

	size_t bufIdx = 0, bufUsed = 0;
	for (size_t vecIdx = 0; vecIdx < vectorCount; vecIdx++) {
		size_t length = vector[vecIdx].length;

		while (length > 0 && bufIdx < fBufferCount) {
			size_t toCopy = std::min<size_t>(length, fBufferSize - bufUsed);
			status_t status = generic_memcpy(
				(generic_addr_t)fBuffers[bufIdx] + bufUsed, false,
				vector[vecIdx].base + (vector[vecIdx].length - length), physical,
				toCopy);
			ASSERT(status == B_OK);

			written += toCopy;
			bufUsed += toCopy;
			length -= toCopy;
			if (bufUsed == fBufferSize) {
				bufIdx++;
				bufUsed = 0;
			}
		}
	}

	TRACE("wrote descriptor (%" B_PRIuSIZE " bytes)\n", written);
	return written;
}


size_t
XhciTransferDesc::Read(generic_io_vec *vector, size_t vectorCount, bool physical)
{
	size_t read = 0;

	size_t bufIdx = 0, bufUsed = 0;
	for (size_t vecIdx = 0; vecIdx < vectorCount; vecIdx++) {
		size_t length = vector[vecIdx].length;

		while (length > 0 && bufIdx < fBufferCount) {
			size_t toCopy = std::min<size_t>(length, fBufferSize - bufUsed);
			status_t status = generic_memcpy(
				vector[vecIdx].base + (vector[vecIdx].length - length), physical,
				(generic_addr_t)fBuffers[bufIdx] + bufUsed, false, toCopy);
			ASSERT(status == B_OK);

			read += toCopy;
			bufUsed += toCopy;
			length -= toCopy;
			if (bufUsed == fBufferSize) {
				bufIdx++;
				bufUsed = 0;
			}
		}
	}

	TRACE("read descriptor (%" B_PRIuSIZE " bytes)\n", read);
	return read;
}


void
XHCI::BuildRoute(UsbBusDevice* hub, uint8 hubPort, uint8& rhPort, uint32& route)
{
	if (hub->Parent() == NULL) {
		if (hub == fRootHub2.GetDevice()) {
			uint8 xhciPort = fRootHub2.GetXHCIPort(hubPort);
			TRACE_ALWAYS("USB 2 port %d -> XHCI port %d\n", hubPort, xhciPort);
			rhPort = xhciPort + 1;
		} else if (hub == fRootHub3.GetDevice()) {
			uint8 xhciPort = fRootHub3.GetXHCIPort(hubPort);
			TRACE_ALWAYS("USB 3 port %d -> XHCI port %d\n", hubPort, xhciPort);
			rhPort = xhciPort + 1;
		} else {
			panic("xhci: unknown root hub\n");
		}
	} else {
		if (hubPort > 15)
			hubPort = 15;

		route = (route << 4) + hubPort;
		BuildRoute(hub->Parent(), hub->HubPort(), rhPort, route);
	}
}


UsbBusDevice*
XHCI::AllocateDevice(UsbBusDevice* parent, int8 hubAddress, uint8 hubPort, usb_speed speed)
{
	TRACE_ALWAYS("AllocateDevice hubAddress %d hubPort %d speed %d\n", hubAddress,
		hubPort, speed);

	uint8 slot = XHCI_MAX_SLOTS;
	status_t status = EnableSlot(&slot);
	if (status != B_OK) {
		TRACE_ERROR("failed to enable slot: %s\n", strerror(status));
		return NULL;
	}

	if (slot == 0 || slot > fSlotCount) {
		TRACE_ERROR("AllocateDevice: bad slot\n");
		return NULL;
	}

	if (fDevices[slot].has_value()) {
		TRACE_ERROR("AllocateDevice: slot already used\n");
		return NULL;
	}

	XhciDevice *device = &fDevices[slot].emplace(this, slot);

	device->fInputCtxArea.SetTo(fStack->AllocateArea((void **)&device->fInputCtx,
		&device->fInputCtxAddr, sizeof(*device->fInputCtx) << fContextSizeShift,
		"XHCI input context"));
	if (!device->fInputCtxArea.IsSet()) {
		TRACE_ERROR("unable to create a input context area\n");
		fDevices[slot].reset();
		return NULL;
	}
	if (fContextSizeShift == 1) {
		// 64-byte contexts have to be page-aligned in order for
		// _OffsetContextAddr to function properly.
		ASSERT((((addr_t)device->fInputCtx) % B_PAGE_SIZE) == 0);
	}

	memset(device->fInputCtx, 0, sizeof(*device->fInputCtx) << fContextSizeShift);
	_WriteContext(&device->fInputCtx->input.dropFlags, 0);
	_WriteContext(&device->fInputCtx->input.addFlags, 3);

	uint8 rhPort = 0;
	uint32 route = 0;
	BuildRoute(parent, hubPort, rhPort, route);

	xhci_slot0 dwslot0 {
		.route = route,
		.num_entries = 1
	};

	// add the speed
	switch (speed) {
	case USB_SPEED_LOWSPEED:
		dwslot0.speed = 2;
		break;
	case USB_SPEED_FULLSPEED:
		dwslot0.speed = 1;
		break;
	case USB_SPEED_HIGHSPEED:
		dwslot0.speed = 3;
		break;
	case USB_SPEED_SUPERSPEED:
		dwslot0.speed = 4;
		break;
	default:
		TRACE_ERROR("unknown usb speed\n");
		break;
	}

	_WriteContext(&device->fInputCtx->slot.dwslot0, dwslot0.value);
	// TODO enable power save
	xhci_slot1 dwslot1 {.rh_port = rhPort};
	_WriteContext(&device->fInputCtx->slot.dwslot1, dwslot1.value);
	xhci_slot2 dwslot2 {.irq_target = 0};

	// If LS/FS device connected to non-root HS device
	if (route != 0 && parent->Speed() == USB_SPEED_HIGHSPEED
		&& (speed == USB_SPEED_LOWSPEED || speed == USB_SPEED_FULLSPEED)) {
		XhciDevice *parenthub = (XhciDevice *)
			parent->ControllerCookie();
		dwslot2.tt_port_num = hubPort;
		dwslot2.tt_hub_slot = parenthub->fSlot;
	}

	_WriteContext(&device->fInputCtx->slot.dwslot2, dwslot2.value);

	xhci_slot3 dwslot3 {.device_address = 0, .slot_state = 0};
	_WriteContext(&device->fInputCtx->slot.dwslot3, dwslot3.value);

	TRACE_ALWAYS("slot 0x%08" B_PRIx32 " 0x%08" B_PRIx32 " 0x%08" B_PRIx32 " 0x%08" B_PRIx32
		"\n", _ReadContext(&device->fInputCtx->slot.dwslot0),
		_ReadContext(&device->fInputCtx->slot.dwslot1),
		_ReadContext(&device->fInputCtx->slot.dwslot2),
		_ReadContext(&device->fInputCtx->slot.dwslot3));

	device->fDeviceCtxArea.SetTo(fStack->AllocateArea((void **)&device->fDeviceCtx,
		&device->fDeviceCtxAddr, sizeof(*device->fDeviceCtx) << fContextSizeShift,
		"XHCI device context"));
	if (!device->fDeviceCtxArea.IsSet()) {
		TRACE_ERROR("unable to create a device context area\n");
		fDevices[slot].reset();
		return NULL;
	}
	memset(device->fDeviceCtx, 0, sizeof(*device->fDeviceCtx) << fContextSizeShift);

	device->fTrbArea.SetTo(fStack->AllocateArea((void **)&device->fTrbs,
		&device->fTrbAddr, sizeof(xhci_trb) * (XHCI_MAX_ENDPOINTS - 1)
			* XHCI_ENDPOINT_RING_SIZE, "XHCI endpoint trbs"));
	if (!device->fTrbArea.IsSet()) {
		TRACE_ERROR("unable to create a device trbs area\n");
		fDevices[slot].reset();
		return NULL;
	}

	// set up slot pointer to device context
	fDcba->baseAddress[slot] = device->fDeviceCtxAddr;

	size_t maxPacketSize;
	switch (speed) {
	case USB_SPEED_LOWSPEED:
	case USB_SPEED_FULLSPEED:
		maxPacketSize = 8;
		break;
	case USB_SPEED_HIGHSPEED:
		maxPacketSize = 64;
		break;
	default:
		maxPacketSize = 512;
		break;
	}

	XhciEndpoint* endpoint0 = &device->fEndpoints[0].emplace(device, 0);
	endpoint0->fTrbs = device->fTrbs;
	endpoint0->fTrbAddr = device->fTrbAddr;

	// configure the Control endpoint 0
	if (endpoint0->Configure(USB_PIPE_CONTROL, false,
			0, maxPacketSize, speed, 0, 0) != B_OK) {
		TRACE_ERROR("unable to configure default control endpoint\n");
		fDevices[slot].reset();
		return NULL;
	}

	// device should get to addressed state (bsr = 0)
	status = SetAddress(device->fInputCtxAddr, false, slot);
	if (status != B_OK) {
		TRACE_ERROR("unable to set address: %s\n", strerror(status));
		fDevices[slot].reset();
		return NULL;
	}

	device->fAddress = xhci_slot3{.value =_ReadContext(
		&device->fDeviceCtx->slot.dwslot3)}.device_address;

#if 0
	TRACE("device: address 0x%x state 0x%08" B_PRIx32 "\n", device->fAddress,
		SLOT_3_SLOT_STATE_GET(_ReadContext(
			&device->fDeviceCtx->slot.dwslot3)));
	TRACE("endpoint0 state 0x%08" B_PRIx32 "\n",
		ENDPOINT_0_STATE_GET(_ReadContext(
			&device->fDeviceCtx->endpoints[0].dwendpoint0)));
#endif

	// Wait a bit for the device to complete addressing
	snooze(USB_DELAY_SET_ADDRESS);

	TRACE("creating new device\n");
	UsbBusDevice *deviceObject = NULL;
	status_t res = fBusManager->CreateDevice(deviceObject, parent, hubAddress, hubPort,
		device->fAddress + 1, speed, device);
	if (res < B_OK) {
		if (res == B_NO_MEMORY) {
			TRACE_ERROR("no memory to allocate device\n");
		} else {
			TRACE_ERROR("device object failed to initialize\n");
		}
		fDevices[slot].reset();
		return NULL;
	}

	TRACE("AllocateDevice() port %d slot %d\n", hubPort, slot);
	return deviceObject;
}


void
XHCI::FreeDevice(UsbBusDevice* usbDevice)
{
	XhciDevice* device = (XhciDevice*)usbDevice->ControllerCookie();
	TRACE("FreeDevice() slot %d\n", device->fSlot);

	// Delete the device first, so it cleans up its pipes and tells us
	// what we need to destroy before we tear down our internal state.
	usbDevice->Free();

	fDevices[device->fSlot].reset();
}


status_t
XHCI::InitDevice(UsbBusDevice* usbDevice, const usb_device_descriptor& deviceDescriptor)
{
	TRACE("device_class: %d device_subclass %d device_protocol %d\n",
		deviceDescriptor.device_class, deviceDescriptor.device_subclass,
		deviceDescriptor.device_protocol);

	void* cookie = usbDevice->ControllerCookie();
	if (cookie != NULL && ((XHCIRootHub*)cookie == &fRootHub2 || (XHCIRootHub*)cookie == &fRootHub3))
		return B_OK;

	XhciDevice* device = (XhciDevice*)cookie;
	usb_speed speed = usbDevice->Speed();

	device->fIsMultiTt = deviceDescriptor.device_class == 9
		&& deviceDescriptor.device_protocol == 2 /* multi-TT */;

	if (speed == USB_SPEED_FULLSPEED && deviceDescriptor.max_packet_size_0 != 8) {
		TRACE("Full speed device with different max packet size for Endpoint 0\n");
		xhci_endpoint1 dwendpoint1 {.value =_ReadContext(
			&device->fInputCtx->endpoints[0].dwendpoint1)};
		dwendpoint1.max_packet_size = deviceDescriptor.max_packet_size_0;
		_WriteContext(&device->fInputCtx->endpoints[0].dwendpoint1,
			dwendpoint1.value);
		_WriteContext(&device->fInputCtx->input.dropFlags, 0);
		_WriteContext(&device->fInputCtx->input.addFlags, (1 << 1));
		EvaluateContext(device->fInputCtxAddr, device->fSlot);
	}

	return B_OK;
}


status_t
XHCI::InitHub(UsbBusDevice* usbDevice, const usb_hub_descriptor& hubDescriptor)
{
	void* cookie = usbDevice->ControllerCookie();
	if (cookie != NULL && ((XHCIRootHub*)cookie == &fRootHub2 || (XHCIRootHub*)cookie == &fRootHub3))
		return B_OK;

	XhciDevice* device = (XhciDevice*)cookie;
	usb_speed speed = usbDevice->Speed();

	xhci_slot0 dwslot0 {.value = _ReadContext(&device->fInputCtx->slot.dwslot0)};
	dwslot0.is_hub = true;
	dwslot0.is_mtt = device->fIsMultiTt;
	_WriteContext(&device->fInputCtx->slot.dwslot0, dwslot0.value);
	xhci_slot1 dwslot1 {.value = _ReadContext(&device->fInputCtx->slot.dwslot1)};
	dwslot1.num_ports = hubDescriptor.num_ports;
	_WriteContext(&device->fInputCtx->slot.dwslot1, dwslot1.value);
	if (speed == USB_SPEED_HIGHSPEED) {
		xhci_slot2 dwslot2 {.value = _ReadContext(&device->fInputCtx->slot.dwslot2)};
		dwslot2.tt_time = HUB_TTT_GET(hubDescriptor.characteristics);
		TRACE_ALWAYS("ttTime: %" B_PRIu32 "\n", dwslot2.tt_time);
		_WriteContext(&device->fInputCtx->slot.dwslot2, dwslot2.value);
	}

	// Wait some time before powering up the ports
	snooze(USB_DELAY_HUB_POWER_UP);

	return B_OK;
}


XhciDevice::~XhciDevice()
{
	if (fSlot != 0) {
		fBase->DisableSlot(fSlot);
		fBase->fDcba->baseAddress[fSlot] = 0;
	}
}


uint8
XHCI::_GetEndpointState(XhciEndpoint* endpoint)
{
	struct xhci_device_ctx* device_ctx = endpoint->fDevice->fDeviceCtx;
	return xhci_endpoint0{.value
		= _ReadContext(&device_ctx->endpoints[endpoint->fId].dwendpoint0)}.state;
}


status_t
XHCI::_InsertEndpointForPipe(UsbBusPipe *pipe)
{
	TRACE("insert endpoint for pipe %p (%d)\n", pipe, pipe->EndpointAddress());

	UsbBusDevice* usbDevice = pipe->GetDevice();
	if (usbDevice->Parent() == NULL) {
		// root hub needs no initialization
		return B_OK;
	}

	XhciDevice *device = (XhciDevice *)
		usbDevice->ControllerCookie();
	if (device == NULL) {
		panic("device is NULL\n");
		return B_NO_INIT;
	}

	const uint8 id = (2 * pipe->EndpointAddress()
		+ (pipe->Direction() != UsbBusPipe::Out ? 1 : 0)) - 1;
	if (id >= XHCI_MAX_ENDPOINTS - 1)
		return B_BAD_VALUE;

	if (id > 0) {
		xhci_slot0 devicedwslot0 {.value = _ReadContext(&device->fDeviceCtx->slot.dwslot0)};
		if (devicedwslot0.num_entries == 1) {
			xhci_slot0 inputdwslot0 {.value = _ReadContext(&device->fInputCtx->slot.dwslot0)};
			inputdwslot0.num_entries = XHCI_MAX_ENDPOINTS - 1;
			_WriteContext(&device->fInputCtx->slot.dwslot0, inputdwslot0.value);
			EvaluateContext(device->fInputCtxAddr, device->fSlot);
		}

		XhciEndpoint* endpoint = &device->fEndpoints[id].emplace(device, id);
		MutexLocker endpointLocker(endpoint->fLock);

		endpoint->fTrbs = device->fTrbs + id * XHCI_ENDPOINT_RING_SIZE;
		endpoint->fTrbAddr = device->fTrbAddr
			+ id * XHCI_ENDPOINT_RING_SIZE * sizeof(xhci_trb);
		memset(endpoint->fTrbs, 0,
			sizeof(xhci_trb) * XHCI_ENDPOINT_RING_SIZE);

		TRACE("insert endpoint for pipe: trbs, device %p endpoint %p\n",
			device->fTrbs, endpoint->fTrbs);
		TRACE("insert endpoint for pipe: trb_addr, device 0x%" B_PRIxPHYSADDR
			" endpoint 0x%" B_PRIxPHYSADDR "\n", device->fTrbAddr,
			endpoint->fTrbAddr);

		const uint8 endpointNum = id + 1;

		status_t status = endpoint->Configure(pipe->Type(),
			pipe->Direction() == UsbBusPipe::In, pipe->Interval(), pipe->MaxPacketSize(),
			usbDevice->Speed(), pipe->MaxBurst(), pipe->BytesPerInterval());
		if (status != B_OK) {
			TRACE_ERROR("unable to configure endpoint: %s\n", strerror(status));
			return status;
		}

		_WriteContext(&device->fInputCtx->input.dropFlags, 0);
		_WriteContext(&device->fInputCtx->input.addFlags,
			(1 << endpointNum) | (1 << 0));

		ConfigureEndpoint(device->fInputCtxAddr, false, device->fSlot);

#if 0
		TRACE("device: address 0x%x state 0x%08" B_PRIx32 "\n",
			device->fAddress, SLOT_3_SLOT_STATE_GET(_ReadContext(
				&device->fDeviceCtx->slot.dwslot3)));
		TRACE("endpoint[0] state 0x%08" B_PRIx32 "\n",
			ENDPOINT_0_STATE_GET(_ReadContext(
				&device->fDeviceCtx->endpoints[0].dwendpoint0)));
		TRACE("endpoint[%d] state 0x%08" B_PRIx32 "\n", id,
			ENDPOINT_0_STATE_GET(_ReadContext(
				&device->fDeviceCtx->endpoints[id].dwendpoint0)));
#endif
	}
	pipe->SetControllerCookie(&(*device->fEndpoints[id]));

	return B_OK;
}


status_t
XHCI::_RemoveEndpointForPipe(UsbBusPipe *pipe)
{
	TRACE("remove endpoint for pipe %p (%d)\n", pipe, pipe->EndpointAddress());

	UsbBusDevice* usbDevice = pipe->GetDevice();
	if (usbDevice->Parent() == NULL)
		return B_BAD_VALUE;

	XhciEndpoint *endpoint = (XhciEndpoint *)pipe->ControllerCookie();
	if (endpoint == NULL || endpoint->fTrbs == NULL)
		return B_NO_INIT;

	pipe->SetControllerCookie(NULL);

	if (endpoint->fId > 0) {
		XhciDevice *device = endpoint->fDevice;
		uint8 epNumber = endpoint->fId + 1;
		StopEndpoint(true, endpoint);

		mutex_lock(&endpoint->fLock);

		// See comment in CancelQueuedTransfers.
		XhciTransferDesc* td;
		while ((td = endpoint->fTransferDescs.RemoveHead()) != NULL)
			delete td;

		device->fEndpoints[endpoint->fId].reset();
		endpoint = NULL;

		_WriteContext(&device->fInputCtx->input.dropFlags, (1 << epNumber));
		_WriteContext(&device->fInputCtx->input.addFlags, (1 << 0));

		// The Deconfigure bit in the Configure Endpoint command indicates
		// that *all* endpoints are to be deconfigured, and not just the ones
		// specified in the context flags. (XHCI 1.2 § 4.6.6 p115.)
		ConfigureEndpoint(device->fInputCtxAddr, false, device->fSlot);
	}

	return B_OK;
}


status_t
XhciEndpoint::LinkDescriptor(XhciTransferDesc *descriptor)
{
	TRACE("link descriptor for pipe\n");

	// Use mutex_trylock first, in case we are in KDL.
	MutexLocker endpointLocker(&fLock, mutex_trylock(&fLock) == B_OK);

	// "used" refers to the number of currently linked TDs, not the number of
	// used TRBs on the ring (we use 2 TRBs on the ring per transfer.)
	if (fUsed >= (XHCI_MAX_TRANSFERS - 1)) {
		TRACE_ERROR("link descriptor for pipe: max transfers count exceeded\n");
		return B_BAD_VALUE;
	}

	// We do not support queuing other transfers in tandem with a fragmented one.
	if (!fTransferDescs.IsEmpty() && fTransferDescs.First()->fTransfer != NULL
			&& fTransferDescs.First()->fTransfer->IsFragmented()) {
		TRACE_ERROR("cannot submit transfer: a fragmented transfer is queued\n");
		return B_DEV_RESOURCE_CONFLICT;
	}

	fUsed++;
	fTransferDescs.Insert(descriptor, false);

	const uint32 current = fCurrent,
		eventdata = current + 1,
		last = XHCI_ENDPOINT_RING_SIZE - 1;
	uint32 next = eventdata + 1;

	TRACE("link descriptor for pipe: current %d, next %d\n", current, next);

	// Add a Link TRB to the end of the descriptor.
	phys_addr_t addr = fTrbAddr + eventdata * sizeof(xhci_trb);
	descriptor->fTrbs[descriptor->fTrbUsed].address = addr;
	descriptor->fTrbs[descriptor->fTrbUsed].status = xhci_trb_status{.irq_target = 0}.value;
	descriptor->fTrbs[descriptor->fTrbUsed].flags = TRB_3_TYPE(TRB_TYPE_LINK)
		| TRB_3_CHAIN_BIT | TRB_3_CYCLE_BIT;
		// It is specified that (XHCI 1.2 § 4.12.3 Note 2 p251) if the TRB
		// following one with the ENT bit set is a Link TRB, the Link TRB
		// shall be evaluated *and* the subsequent TRB shall be. Thus a
		// TRB_3_ENT_BIT is unnecessary here; and from testing seems to
		// break all transfers on a (very) small number of controllers.

#if !B_HOST_IS_LENDIAN
	// Convert endianness.
	for (uint32 i = 0; i <= descriptor->fTrbUsed; i++) {
		descriptor->fTrbs[i].address =
			B_HOST_TO_LENDIAN_INT64(descriptor->fTrbs[i].address);
		descriptor->fTrbs[i].status =
			B_HOST_TO_LENDIAN_INT32(descriptor->fTrbs[i].status);
		descriptor->fTrbs[i].flags =
			B_HOST_TO_LENDIAN_INT32(descriptor->fTrbs[i].flags);
	}
#endif

	// Link the descriptor.
	fTrbs[current].address =
		B_HOST_TO_LENDIAN_INT64(descriptor->fTrbAddr);
	fTrbs[current].status =
		B_HOST_TO_LENDIAN_INT32(xhci_trb_status{.irq_target = 0}.value);
	fTrbs[current].flags =
		B_HOST_TO_LENDIAN_INT32(TRB_3_TYPE(TRB_TYPE_LINK));

	// Set up the Event Data TRB (XHCI 1.2 § 4.11.5.2 p230.)
	//
	// We do this on the main ring for two reasons: first, to avoid a small
	// potential race between the interrupt and the controller evaluating
	// the link TRB to get back onto the ring; and second, because many
	// controllers throw errors if the target of a Link TRB is not valid
	// (i.e. does not have its Cycle Bit set.)
	//
	// We also set the "address" field, which the controller will copy
	// verbatim into the TRB it posts to the event ring, to be the last
	// "real" TRB in the TD; this will allow us to determine what transfer
	// the resulting Transfer Event TRB refers to.
	fTrbs[eventdata].address =
		B_HOST_TO_LENDIAN_INT64(descriptor->fTrbAddr
			+ (descriptor->fTrbUsed - 1) * sizeof(xhci_trb));
	fTrbs[eventdata].status =
		B_HOST_TO_LENDIAN_INT32(xhci_trb_status{.irq_target = 0}.value);
	fTrbs[eventdata].flags =
		B_HOST_TO_LENDIAN_INT32(TRB_3_TYPE(TRB_TYPE_EVENT_DATA)
			| TRB_3_IOC_BIT | TRB_3_CYCLE_BIT);

	if (next == last) {
		// We always use 2 TRBs per _Link..() call, so if "next" is the last
		// TRB in the ring, we need to generate a link TRB at "next", and
		// then wrap it to 0. (We write the cycle bit later, after wrapping,
		// for the reason noted in the previous comment.)
		fTrbs[next].address =
			B_HOST_TO_LENDIAN_INT64(fTrbAddr);
		fTrbs[next].status =
			B_HOST_TO_LENDIAN_INT32(xhci_trb_status{.irq_target = 0}.value);
		fTrbs[next].flags =
			B_HOST_TO_LENDIAN_INT32(TRB_3_TYPE(TRB_TYPE_LINK));

		next = 0;
	}

	fTrbs[next].address = 0;
	fTrbs[next].status = 0;
	fTrbs[next].flags = 0;

	memory_write_barrier();

	// Everything is ready, so write the cycle bit(s).
	fTrbs[current].flags |= B_HOST_TO_LENDIAN_INT32(TRB_3_CYCLE_BIT);
	if (current == 0 && fTrbs[last].address != 0)
		fTrbs[last].flags |= B_HOST_TO_LENDIAN_INT32(TRB_3_CYCLE_BIT);

	TRACE("_LinkDescriptorForPipe pCurrent %p phys 0x%" B_PRIxPHYSADDR
		" 0x%" B_PRIxPHYSADDR " 0x%08" B_PRIx32 "\n", &fTrbs[current],
		fTrbAddr + current * sizeof(struct xhci_trb),
		fTrbs[current].address,
		B_LENDIAN_TO_HOST_INT32(fTrbs[current].flags));

	fCurrent = next;
	endpointLocker.Unlock();

#ifdef TRACE_USB
	XHCI* xhci = fDevice->fBase;
#endif

	TRACE("Endpoint status 0x%08" B_PRIx32 " 0x%08" B_PRIx32 " 0x%016" B_PRIx64 "\n",
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].dwendpoint0),
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].dwendpoint1),
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].qwendpoint2));

	fDevice->fBase->Ring(fDevice->fSlot, fId + 1);

	TRACE("Endpoint status 0x%08" B_PRIx32 " 0x%08" B_PRIx32 " 0x%016" B_PRIx64 "\n",
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].dwendpoint0),
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].dwendpoint1),
		xhci->_ReadContext(&fDevice->fDeviceCtx->endpoints[fId].qwendpoint2));

	return B_OK;
}


status_t
XhciEndpoint::UnlinkDescriptor(XhciTransferDesc *descriptor)
{
	TRACE("unlink descriptor for pipe\n");
	// We presume that the caller has already locked or owns the endpoint.

	if (!fTransferDescs.Contains(descriptor))
		return B_ERROR;

	fUsed--;
	fTransferDescs.Remove(descriptor);
	return B_OK;
}


status_t
XhciEndpoint::Configure(uint8 type,
	bool directionIn, uint16 interval, uint16 maxPacketSize, usb_speed speed,
	uint8 maxBurst, uint16 bytesPerInterval)
{
	xhci_endpoint0 dwendpoint0 {};
	xhci_endpoint1 dwendpoint1 {};
	uint64 qwendpoint2 = 0;
	xhci_endpoint4 dwendpoint4 {};

	// Compute and assign the endpoint type. (XHCI 1.2 § 6.2.3 Table 6-9 p452.)
	uint8 xhciType = 4;
	if (type == USB_PIPE_INTERRUPT)
		xhciType = 3;
	if (type == USB_PIPE_BULK)
		xhciType = 2;
	if (type == USB_PIPE_ISO)
		xhciType = 1;
	xhciType |= directionIn ? (1 << 2) : 0;
	dwendpoint1.ep_type = xhciType;

	// Compute and assign interval. (XHCI 1.2 § 6.2.3.6 p456.)
	uint16 calcInterval;
	if (type == USB_PIPE_BULK || type == USB_PIPE_CONTROL) {
		// Bulk and Control endpoints never issue NAKs.
		calcInterval = 0;
	} else {
		switch (speed) {
		case USB_SPEED_FULLSPEED:
			if (type == USB_PIPE_ISO) {
				// Convert 1-16 into 3-18.
				calcInterval = min_c(max_c(interval, 1), 16) + 2;
				break;
			}

			// fall through
		case USB_SPEED_LOWSPEED: {
			// Convert 1ms-255ms into 3-10.

			// Find the index of the highest set bit in "interval".
			uint32 temp = min_c(max_c(interval, 1), 255);
			for (calcInterval = 0; temp != 1; calcInterval++)
				temp = temp >> 1;
			calcInterval += 3;
			break;
		}

		case USB_SPEED_HIGHSPEED:
		case USB_SPEED_SUPERSPEED:
		default:
			// Convert 1-16 into 0-15.
			calcInterval = min_c(max_c(interval, 1), 16) - 1;
			break;
		}
	}
	dwendpoint0.interval = calcInterval;

	// For non-isochronous endpoints, we want the controller to retry failed
	// transfers, if possible. (XHCI 1.2 § 4.10.2.3 p197.)
	if (type != USB_PIPE_ISO)
		dwendpoint1.c_err = 3;

	// Assign maximum burst size. For USB3 devices this is passed in; for
	// all other devices we compute it. (XHCI 1.2 § 4.8.2 p161.)
	if (speed == USB_SPEED_HIGHSPEED && (type == USB_PIPE_INTERRUPT
			|| type == USB_PIPE_ISO)) {
		maxBurst = (maxPacketSize & 0x1800) >> 11;
	} else if (speed != USB_SPEED_SUPERSPEED) {
		maxBurst = 0;
	}
	dwendpoint1.max_burst = maxBurst;

	// Assign maximum packet size, set the ring address, and set the
	// "Dequeue Cycle State" bit. (XHCI 1.2 § 6.2.3 Table 6-10 p453.)
	dwendpoint1.max_packet_size = maxPacketSize;
	qwendpoint2 |= ENDPOINT_2_DCS_BIT | fTrbAddr;

	// The Max Burst Payload is the number of bytes moved by a
	// maximum sized burst. (XHCI 1.2 § 4.11.7.1 p236.)
	fMaxBurstPayload = (maxBurst + 1) * maxPacketSize;
	if (fMaxBurstPayload == 0) {
		TRACE_ERROR("ConfigureEndpoint() failed invalid max_burst_payload\n");
		return B_BAD_VALUE;
	}

	// Assign average TRB length.
	if (type == USB_PIPE_CONTROL) {
		// Control pipes are a special case, as they rarely have
		// outbound transfers of any substantial size.
		dwendpoint4.avg_trb_length = 8;
	} else if (type == USB_PIPE_ISO) {
		// Isochronous pipes are another special case: the TRB size will be
		// one packet (which is normally smaller than the max packet size,
		// but we don't know what it is here.)
		dwendpoint4.avg_trb_length = maxPacketSize;
	} else {
		// Under all other circumstances, we put max_burst_payload in a TRB.
		dwendpoint4.avg_trb_length = fMaxBurstPayload;
	}

	// Assign maximum ESIT payload. (XHCI 1.2 § 4.14.2 p259.)
	if (type == USB_PIPE_INTERRUPT || type == USB_PIPE_ISO) {
		// TODO: For SuperSpeedPlus endpoints, there is yet another descriptor
		// for isochronous endpoints that specifies the maximum ESIT payload.
		// We don't fetch this yet, so just fall back to the USB2 computation
		// method if bytesPerInterval is 0.
		if (speed == USB_SPEED_SUPERSPEED && bytesPerInterval != 0)
			dwendpoint4.max_esit_payload_lo = bytesPerInterval;
		else /*if (speed >= USB_SPEED_HIGHSPEED)*/
			dwendpoint4.max_esit_payload_lo = (maxBurst + 1) * maxPacketSize;
	}

	XHCI* xhci = fDevice->fBase;

	struct xhci_endpoint_ctx& endpointCtx = fDevice->fInputCtx->endpoints[fId];
	xhci->_WriteContext(&endpointCtx.dwendpoint0, dwendpoint0.value);
	xhci->_WriteContext(&endpointCtx.dwendpoint1, dwendpoint1.value);
	xhci->_WriteContext(&endpointCtx.qwendpoint2, qwendpoint2);
	xhci->_WriteContext(&endpointCtx.dwendpoint4, dwendpoint4.value);

#ifdef TRACE_USB
	dprintf("endpoint[%u]: ", fId);
	xhci->DumpEndpointState(fDevice->fInputCtx->endpoints[fId]);
#endif

	return B_OK;
}


status_t
XHCI::GetPortSpeed(uint8 index, usb_speed* speed)
{
	if (index >= fPortCount)
		return B_BAD_INDEX;

	uint32 portStatus = ReadOpReg(XHCI_PORTSC(index));

	switch (PS_SPEED_GET(portStatus)) {
	case 2:
		*speed = USB_SPEED_LOWSPEED;
		break;
	case 1:
		*speed = USB_SPEED_FULLSPEED;
		break;
	case 3:
		*speed = USB_SPEED_HIGHSPEED;
		break;
	case 4:
		*speed = USB_SPEED_SUPERSPEED;
		break;
	default:
		TRACE_ALWAYS("nonstandard port speed %" B_PRId32 ", assuming SuperSpeed\n",
			PS_SPEED_GET(portStatus));
		*speed = USB_SPEED_SUPERSPEED;
		break;
	}

	return B_OK;
}


status_t
XHCI::GetPortStatus(uint8 index, usb_port_status* status)
{
	if (index >= fPortCount)
		return B_BAD_INDEX;

	status->status = status->change = 0;
	uint32 portStatus = ReadOpReg(XHCI_PORTSC(index));
	TRACE("port %" B_PRId8 " status=0x%08" B_PRIx32 "\n", index, portStatus);

	// build the status
	switch (PS_SPEED_GET(portStatus)) {
	case 3:
		status->status |= PORT_STATUS_HIGH_SPEED;
		break;
	case 2:
		status->status |= PORT_STATUS_LOW_SPEED;
		break;
	default:
		break;
	}

	if (portStatus & PS_CCS)
		status->status |= PORT_STATUS_CONNECTION;
	if (portStatus & PS_PED)
		status->status |= PORT_STATUS_ENABLE;
	if (portStatus & PS_OCA)
		status->status |= PORT_STATUS_OVER_CURRENT;
	if (portStatus & PS_PR)
		status->status |= PORT_STATUS_RESET;
	if (portStatus & PS_PP) {
		if (fPortSpeeds[index] == USB_SPEED_SUPERSPEED)
			status->status |= PORT_STATUS_SS_POWER;
		else
			status->status |= PORT_STATUS_POWER;
	}
	if (fPortSpeeds[index] == USB_SPEED_SUPERSPEED)
		status->status |= portStatus & PS_PLS_MASK;

	// build the change
	if (portStatus & PS_CSC)
		status->change |= PORT_STATUS_CONNECTION;
	if (portStatus & PS_PEC)
		status->change |= PORT_STATUS_ENABLE;
	if (portStatus & PS_OCC)
		status->change |= PORT_STATUS_OVER_CURRENT;
	if (portStatus & PS_PRC)
		status->change |= PORT_STATUS_RESET;

	if (fPortSpeeds[index] == USB_SPEED_SUPERSPEED) {
		if (portStatus & PS_PLC)
			status->change |= PORT_CHANGE_LINK_STATE;
		if (portStatus & PS_WRC)
			status->change |= PORT_CHANGE_BH_PORT_RESET;
	}

	return B_OK;
}


status_t
XHCI::SetPortFeature(uint8 index, uint16 feature)
{
	TRACE("set port feature index %u feature %u\n", index, feature);
	if (index >= fPortCount)
		return B_BAD_INDEX;

	uint32 portRegister = XHCI_PORTSC(index);
	uint32 portStatus = ReadOpReg(portRegister) & ~PS_CLEAR;

	switch (feature) {
	case PORT_SUSPEND:
		if ((portStatus & PS_PED) == 0 || (portStatus & PS_PR)
			|| (portStatus & PS_PLS_MASK) >= PS_XDEV_U3) {
			TRACE_ERROR("USB core suspending device not in U0/U1/U2.\n");
			return B_BAD_VALUE;
		}
		portStatus &= ~PS_PLS_MASK;
		WriteOpReg(portRegister, portStatus | PS_LWS | PS_XDEV_U3);
		break;

	case PORT_RESET:
		WriteOpReg(portRegister, portStatus | PS_PR);
		break;

	case PORT_POWER:
		WriteOpReg(portRegister, portStatus | PS_PP);
		break;
	default:
		return B_BAD_VALUE;
	}
	ReadOpReg(portRegister);
	return B_OK;
}


status_t
XHCI::ClearPortFeature(uint8 index, uint16 feature)
{
	TRACE("clear port feature index %u feature %u\n", index, feature);
	if (index >= fPortCount)
		return B_BAD_INDEX;

	uint32 portRegister = XHCI_PORTSC(index);
	uint32 portStatus = ReadOpReg(portRegister) & ~PS_CLEAR;

	switch (feature) {
	case PORT_SUSPEND:
		portStatus = ReadOpReg(portRegister);
		if (portStatus & PS_PR)
			return B_BAD_VALUE;
		if (portStatus & PS_XDEV_U3) {
			if ((portStatus & PS_PED) == 0)
				return B_BAD_VALUE;
			portStatus &= ~PS_PLS_MASK;
			WriteOpReg(portRegister, portStatus | PS_XDEV_U0 | PS_LWS);
		}
		break;
	case PORT_ENABLE:
		WriteOpReg(portRegister, portStatus | PS_PED);
		break;
	case PORT_POWER:
		WriteOpReg(portRegister, portStatus & ~PS_PP);
		break;
	case C_PORT_CONNECTION:
		WriteOpReg(portRegister, portStatus | PS_CSC);
		break;
	case C_PORT_ENABLE:
		WriteOpReg(portRegister, portStatus | PS_PEC);
		break;
	case C_PORT_OVER_CURRENT:
		WriteOpReg(portRegister, portStatus | PS_OCC);
		break;
	case C_PORT_RESET:
		WriteOpReg(portRegister, portStatus | PS_PRC);
		break;
	case C_PORT_BH_PORT_RESET:
		WriteOpReg(portRegister, portStatus | PS_WRC);
		break;
	case C_PORT_LINK_STATE:
		WriteOpReg(portRegister, portStatus | PS_PLC);
		break;
	default:
		return B_BAD_VALUE;
	}

	ReadOpReg(portRegister);
	return B_OK;
}


status_t
XHCI::ControllerHalt()
{
	// Mask off run state
	WriteOpReg(XHCI_CMD, ReadOpReg(XHCI_CMD) & ~CMD_RUN);

	// wait for shutdown state
	if (WaitOpBits(XHCI_STS, STS_HCH, STS_HCH) != B_OK) {
		TRACE_ERROR("HCH shutdown timeout\n");
		return B_ERROR;
	}
	return B_OK;
}


status_t
XHCI::ControllerReset()
{
	TRACE("ControllerReset() cmd: 0x%" B_PRIx32 " sts: 0x%" B_PRIx32 "\n",
		ReadOpReg(XHCI_CMD), ReadOpReg(XHCI_STS));
	WriteOpReg(XHCI_CMD, ReadOpReg(XHCI_CMD) | CMD_HCRST);

	if (WaitOpBits(XHCI_CMD, CMD_HCRST, 0) != B_OK) {
		TRACE_ERROR("ControllerReset() failed CMD_HCRST\n");
		return B_ERROR;
	}

	if (WaitOpBits(XHCI_STS, STS_CNR, 0) != B_OK) {
		TRACE_ERROR("ControllerReset() failed STS_CNR\n");
		return B_ERROR;
	}

	return B_OK;
}


int32
XHCI::InterruptHandler(void* data)
{
	return ((XHCI*)data)->Interrupt();
}


int32
XHCI::Interrupt()
{
	SpinLocker _(&fSpinlock);

	uint32 status = ReadOpReg(XHCI_STS);
	uint32 temp = ReadRunReg32(XHCI_IMAN(0));
	WriteOpReg(XHCI_STS, status);
	WriteRunReg32(XHCI_IMAN(0), temp);

	int32 result = B_HANDLED_INTERRUPT;

	if ((status & STS_HCH) != 0) {
		TRACE_ERROR("Host Controller halted\n");
		return result;
	}
	if ((status & STS_HSE) != 0) {
		TRACE_ERROR("Host System Error\n");
		return result;
	}
	if ((status & STS_HCE) != 0) {
		TRACE_ERROR("Host Controller Error\n");
		return result;
	}

	if ((status & STS_EINT) == 0) {
		TRACE("STS: 0x%" B_PRIx32 " IRQ_PENDING: 0x%" B_PRIx32 "\n",
			status, temp);
		return B_UNHANDLED_INTERRUPT;
	}

	TRACE("Event Interrupt\n");
	release_sem_etc(fEventSem, 1, B_DO_NOT_RESCHEDULE);
	return B_INVOKE_SCHEDULER;
}


void
XHCI::Ring(uint8 slot, uint8 endpoint)
{
	TRACE("Ding Dong! slot:%d endpoint %d\n", slot, endpoint);
	if ((slot == 0 && endpoint > 0) || (slot > 0 && endpoint == 0))
		panic("Ring() invalid slot/endpoint combination\n");
	if (slot > fSlotCount || endpoint >= XHCI_MAX_ENDPOINTS)
		panic("Ring() invalid slot or endpoint\n");

	WriteDoorReg32(XHCI_DOORBELL(slot), XHCI_DOORBELL_TARGET(endpoint)
		| XHCI_DOORBELL_STREAMID(0));
	ReadDoorReg32(XHCI_DOORBELL(slot));
		// Flush PCI writes
}


void
XHCI::QueueCommand(xhci_trb* trb)
{
	uint8 i, j;
	uint32 temp;

	i = fCmdIdx;
	j = fCmdCcs;

	TRACE("command[%u] = %" B_PRId32 " (0x%016" B_PRIx64 ", 0x%08" B_PRIx32
		", 0x%08" B_PRIx32 ")\n", i, TRB_3_TYPE_GET(trb->flags), trb->address,
		trb->status, trb->flags);

	fCmdRing[i].address = trb->address;
	fCmdRing[i].status = trb->status;
	temp = trb->flags;

	if (j)
		temp |= TRB_3_CYCLE_BIT;
	else
		temp &= ~TRB_3_CYCLE_BIT;
	temp &= ~TRB_3_TC_BIT;
	fCmdRing[i].flags = B_HOST_TO_LENDIAN_INT32(temp);

	fCmdAddr = fErst->rs_addr + (XHCI_MAX_EVENTS + i) * sizeof(xhci_trb);

	i++;

	if (i == (XHCI_MAX_COMMANDS - 1)) {
		temp = TRB_3_TYPE(TRB_TYPE_LINK) | TRB_3_TC_BIT;
		if (j)
			temp |= TRB_3_CYCLE_BIT;
		fCmdRing[i].flags = B_HOST_TO_LENDIAN_INT32(temp);

		i = 0;
		j ^= 1;
	}

	fCmdIdx = i;
	fCmdCcs = j;
}


void
XHCI::HandleCmdComplete(xhci_trb* trb)
{
	if (fCmdAddr == trb->address) {
		TRACE("Received command event\n");
		fCmdResult[0] = trb->status;
		fCmdResult[1] = B_LENDIAN_TO_HOST_INT32(trb->flags);
		release_sem_etc(fCmdCompSem, 1, B_DO_NOT_RESCHEDULE);
	} else
		TRACE_ERROR("received command event for unknown command!\n");
}


void
XHCI::HandleTransferComplete(xhci_trb* trb)
{
	const uint32 flags = B_LENDIAN_TO_HOST_INT32(trb->flags);
	const uint8 endpointNumber = TRB_3_ENDPOINT_GET(flags);
	const uint8 slot = TRB_3_SLOT_GET(flags);

	if (slot > fSlotCount)
		TRACE_ERROR("invalid slot\n");
	if (endpointNumber == 0 || endpointNumber >= XHCI_MAX_ENDPOINTS) {
		TRACE_ERROR("invalid endpoint\n");
		return;
	}

	XhciDevice *device = &(*fDevices[slot]);
	XhciEndpoint *endpoint = &(*device->fEndpoints[endpointNumber - 1]);

	if (endpoint->fTrbs == NULL) {
		TRACE_ERROR("got TRB but endpoint is not allocated!\n");
		return;
	}

	// Use mutex_trylock first, in case we are in KDL.
	MutexLocker endpointLocker(endpoint->fLock, mutex_trylock(&endpoint->fLock) == B_OK);
	if (!endpointLocker.IsLocked()) {
		// We failed to get the lock. Most likely it was destroyed
		// while we were waiting for it.
		return;
	}

	// In the case of an Event Data TRB, the "transferred" field refers
	// to the actual number of bytes transferred across the whole TD.
	// (XHCI 1.2 § 6.4.2.1 Table 6-38 p478.)
	const uint8 completionCode = TRB_2_COMP_CODE_GET(trb->status);
	int32 transferred = TRB_2_REM_GET(trb->status), remainder = -1;

	TRACE("HandleTransferComplete: ed %" B_PRIu32 ", code %" B_PRIu8 ", transferred %" B_PRId32 "\n",
		  (flags & TRB_3_EVENT_DATA_BIT), completionCode, transferred);

	if ((flags & TRB_3_EVENT_DATA_BIT) == 0) {
		// This should only occur under error conditions.
		TRACE("got an interrupt for a non-Event Data TRB!\n");
		remainder = transferred;
		transferred = -1;
	}

	if (completionCode != COMP_SUCCESS && completionCode != COMP_SHORT_PACKET
			&& completionCode != COMP_STOPPED) {
		TRACE_ALWAYS("transfer error on slot %" B_PRId8 " endpoint %" B_PRId8
			": %s\n", slot, endpointNumber, xhci_error_string(completionCode));
	}

	const phys_addr_t source = B_LENDIAN_TO_HOST_INT64(trb->address);
	for (XhciTransferDesc *td = endpoint->fTransferDescs.First(); td != NULL; td = endpoint->fTransferDescs.GetNext(td)) {
		int64 offset = (source - td->fTrbAddr) / sizeof(xhci_trb);
		if (offset < 0 || offset >= td->fTrbCount)
			continue;

		TRACE("HandleTransferComplete td %p trb %" B_PRId64 " found\n",
			td, offset);

		// The TRB at offset fTrbUsed will be the link TRB, which we do not
		// care about (and should not generate an interrupt at all.) We really
		// care about the properly last TRB, at index "count - 1", which the
		// Event Data TRB that _LinkDescriptorForPipe creates points to.
		//
		// But if we have an unsuccessful completion code, the transfer
		// likely failed midway; so just accept it anyway.
		if (offset == (td->fTrbUsed - 1) || completionCode != COMP_SUCCESS) {
			endpoint->UnlinkDescriptor(td);
			endpointLocker.Unlock();

			td->fTrbCompletionCode = completionCode;
			td->fTdTransferred = transferred;
			td->fTrbLeft = remainder;

			// add descriptor to finished list
			if (mutex_trylock(&fFinishedLock) != B_OK)
				mutex_lock(&fFinishedLock);

			fFinishedList.Insert(td, false);
			mutex_unlock(&fFinishedLock);

			release_sem_etc(fFinishTransfersSem, 1, B_DO_NOT_RESCHEDULE);
			TRACE("HandleTransferComplete td %p done\n", td);
		} else {
			TRACE_ERROR("successful TRB 0x%" B_PRIxPHYSADDR " was found, but it wasn't "
				"the last in the TD!\n", source);
		}
		return;
	}
	TRACE_ERROR("TRB 0x%" B_PRIxPHYSADDR " was not found in the endpoint!\n", source);
}


void
XHCI::DumpRing(xhci_trb *trbs, uint32 size)
{
	if (!Lock()) {
		TRACE("Unable to get lock!\n");
		return;
	}

	for (uint32 i = 0; i < size; i++) {
		TRACE("command[%" B_PRId32 "] = %" B_PRId32 " (0x%016" B_PRIx64 ","
			" 0x%08" B_PRIx32 ", 0x%08" B_PRIx32 ")\n", i,
			TRB_3_TYPE_GET(B_LENDIAN_TO_HOST_INT32(trbs[i].flags)),
			trbs[i].address, trbs[i].status, trbs[i].flags);
	}

	Unlock();
}


status_t
XHCI::DoCommand(xhci_trb* trb)
{
	if (!Lock()) {
		TRACE("Unable to get lock!\n");
		return B_ERROR;
	}

	QueueCommand(trb);
	Ring(0, 0);

	// Begin with a 50ms timeout.
	if (acquire_sem_etc(fCmdCompSem, 1, B_RELATIVE_TIMEOUT, 50 * 1000) != B_OK) {
		// We've hit the timeout. In some error cases, interrupts are not
		// generated; so here we force the event ring to be polled once.
		release_sem(fEventSem);

		// Now try again, this time with a 750ms timeout.
		if (acquire_sem_etc(fCmdCompSem, 1, B_RELATIVE_TIMEOUT,
				750 * 1000) != B_OK) {
			TRACE("Unable to obtain fCmdCompSem!\n");
			fCmdAddr = 0;
			Unlock();
			return B_TIMED_OUT;
		}
	}

	// eat up sems that have been released by multiple interrupts
	int32 semCount = 0;
	get_sem_count(fCmdCompSem, &semCount);
	if (semCount > 0)
		acquire_sem_etc(fCmdCompSem, semCount, B_RELATIVE_TIMEOUT, 0);

	status_t status = B_OK;
	uint32 completionCode = TRB_2_COMP_CODE_GET(fCmdResult[0]);
	TRACE("command complete\n");
	if (completionCode != COMP_SUCCESS) {
		TRACE_ERROR("unsuccessful command %" B_PRId32 ", error %s (%" B_PRId32 ")\n",
			TRB_3_TYPE_GET(trb->flags), xhci_error_string(completionCode),
			completionCode);
		status = B_IO_ERROR;
	}

	trb->status = fCmdResult[0];
	trb->flags = fCmdResult[1];

	fCmdAddr = 0;
	Unlock();
	return status;
}


// #pragma mark - Commands

status_t
XHCI::Noop()
{
	TRACE("Issue No-Op\n");
	xhci_trb trb {
		.flags = TRB_3_TYPE(TRB_TYPE_CMD_NOOP)
	};

	return DoCommand(&trb);
}


status_t
XHCI::EnableSlot(uint8* slot)
{
	TRACE("Enable Slot\n");
	xhci_trb trb {
		.flags = TRB_3_TYPE(TRB_TYPE_ENABLE_SLOT)
	};

	CHECK_RET(DoCommand(&trb));

	*slot = TRB_3_SLOT_GET(trb.flags);
	return *slot != 0 ? B_OK : B_BAD_VALUE;
}


status_t
XHCI::DisableSlot(uint8 slot)
{
	TRACE("Disable Slot\n");
	xhci_trb trb {
		.flags = (uint32)(TRB_3_TYPE(TRB_TYPE_DISABLE_SLOT) | TRB_3_SLOT(slot))
	};

	return DoCommand(&trb);
}


status_t
XHCI::SetAddress(uint64 inputContext, bool bsr, uint8 slot)
{
	TRACE("Set Address\n");
	xhci_trb trb {
		.address = inputContext,
		.flags = (uint32)(TRB_3_TYPE(TRB_TYPE_ADDRESS_DEVICE) | TRB_3_SLOT(slot))
	};

	if (bsr)
		trb.flags |= TRB_3_BSR_BIT;

	return DoCommand(&trb);
}


status_t
XHCI::ConfigureEndpoint(uint64 inputContext, bool deconfigure, uint8 slot)
{
	TRACE("Configure Endpoint\n");
	xhci_trb trb {
		.address = inputContext,
		.flags = (uint32)(TRB_3_TYPE(TRB_TYPE_CONFIGURE_ENDPOINT) | TRB_3_SLOT(slot))
	};

	if (deconfigure)
		trb.flags |= TRB_3_DCEP_BIT;

	return DoCommand(&trb);
}


status_t
XHCI::EvaluateContext(uint64 inputContext, uint8 slot)
{
	TRACE("Evaluate Context\n");
	xhci_trb trb {
		.address = inputContext,
		.flags = (uint32)(TRB_3_TYPE(TRB_TYPE_EVALUATE_CONTEXT) | TRB_3_SLOT(slot))
	};

	return DoCommand(&trb);
}


status_t
XHCI::ResetEndpoint(bool preserve, XhciEndpoint* endpoint)
{
	TRACE("Reset Endpoint\n");

	switch (_GetEndpointState(endpoint)) {
		case ENDPOINT_STATE_STOPPED:
			TRACE("Reset Endpoint: already stopped");
			return B_OK;
		case ENDPOINT_STATE_HALTED:
			TRACE("Reset Endpoint: warning, weird state!");
		default:
			break;
	}

	xhci_trb trb {
		.flags = (uint32)(
			TRB_3_TYPE(TRB_TYPE_RESET_ENDPOINT) |
			TRB_3_SLOT(endpoint->fDevice->fSlot) |
			TRB_3_ENDPOINT(endpoint->fId + 1))
	};
	if (preserve)
		trb.flags |= TRB_3_PRSV_BIT;

	return DoCommand(&trb);
}


status_t
XHCI::StopEndpoint(bool suspend, XhciEndpoint* endpoint)
{
	TRACE("Stop Endpoint\n");

	switch (_GetEndpointState(endpoint)) {
		case ENDPOINT_STATE_HALTED:
			TRACE("Stop Endpoint: error, halted");
			return B_DEV_STALLED;
		case ENDPOINT_STATE_STOPPED:
			TRACE("Stop Endpoint: already stopped");
			return B_OK;
		default:
			break;
	}

	xhci_trb trb {
		.flags = (uint32)(
			TRB_3_TYPE(TRB_TYPE_STOP_ENDPOINT) |
			TRB_3_SLOT(endpoint->fDevice->fSlot) |
			TRB_3_ENDPOINT(endpoint->fId + 1))
	};
	if (suspend)
		trb.flags |= TRB_3_SUSPEND_ENDPOINT_BIT;

	return DoCommand(&trb);
}


status_t
XHCI::SetTRDequeue(uint64 dequeue, uint16 stream, uint8 endpoint, uint8 slot)
{
	TRACE("Set TR Dequeue\n");
	xhci_trb trb {
		.address = dequeue | ENDPOINT_2_DCS_BIT,
			// The DCS bit is copied from the address field as in ConfigureEndpoint.
			// (XHCI 1.2 § 4.6.10 p142.)
		.status = (uint32)(TRB_2_STREAM(stream)),
		.flags = (uint32)(
			TRB_3_TYPE(TRB_TYPE_SET_TR_DEQUEUE) |
			TRB_3_SLOT(slot) |
			TRB_3_ENDPOINT(endpoint))
	};

	return DoCommand(&trb);
}


status_t
XHCI::ResetDevice(uint8 slot)
{
	TRACE("Reset Device\n");
	xhci_trb trb {
		.flags = (uint32)(TRB_3_TYPE(TRB_TYPE_RESET_DEVICE) | TRB_3_SLOT(slot))
	};

	return DoCommand(&trb);
}


// #pragma mark -

int32
XHCI::EventThread(void* data)
{
	((XHCI *)data)->CompleteEvents();
	return B_OK;
}


void
XHCI::CompleteEvents()
{
	while (!fStopThreads) {
		if (acquire_sem(fEventSem) < B_OK)
			continue;

		// eat up sems that have been released by multiple interrupts
		int32 semCount = 0;
		get_sem_count(fEventSem, &semCount);
		if (semCount > 0)
			acquire_sem_etc(fEventSem, semCount, B_RELATIVE_TIMEOUT, 0);

		ProcessEvents();
	}
}


void
XHCI::ProcessEvents()
{
	// Use mutex_trylock first, in case we are in KDL.
	MutexLocker locker(fEventLock, mutex_trylock(&fEventLock) == B_OK);
	if (!locker.IsLocked()) {
		// We failed to get the lock. This really should not happen.
		TRACE_ERROR("failed to acquire event lock!\n");
		return;
	}

	uint16 i = fEventIdx;
	uint8 j = fEventCcs;
	uint8 t = 2;

	while (1) {
		uint32 temp = B_LENDIAN_TO_HOST_INT32(fEventRing[i].flags);
		uint8 event = TRB_3_TYPE_GET(temp);
		TRACE("event[%u] = %u (0x%016" B_PRIx64 " 0x%08" B_PRIx32 " 0x%08"
			B_PRIx32 ")\n", i, event, fEventRing[i].address,
			fEventRing[i].status, B_LENDIAN_TO_HOST_INT32(fEventRing[i].flags));
		uint8 k = (temp & TRB_3_CYCLE_BIT) ? 1 : 0;
		if (j != k)
			break;

		switch (event) {
		case TRB_TYPE_COMMAND_COMPLETION:
			HandleCmdComplete(&fEventRing[i]);
			break;
		case TRB_TYPE_TRANSFER:
			HandleTransferComplete(&fEventRing[i]);
			break;
		case TRB_TYPE_PORT_STATUS_CHANGE: {
			uint32 portNo = (uint32)fEventRing[i].address >> 24;
			if (portNo < 1 || portNo - 1 >= fPortCount)
				break;

			if (fPortSpeeds[portNo - 1] == USB_SPEED_SUPERSPEED)
				fRootHub3.PortStatusChanged(fRootHubPorts[portNo - 1]);
			else
				fRootHub2.PortStatusChanged(fRootHubPorts[portNo - 1]);

			break;
		}
		default:
			TRACE_ERROR("Unhandled event = %u\n", event);
			break;
		}

		i++;
		if (i == XHCI_MAX_EVENTS) {
			i = 0;
			j ^= 1;
			if (!--t)
				break;
		}
	}

	fEventIdx = i;
	fEventCcs = j;

	uint64 addr = fErst->rs_addr + i * sizeof(xhci_trb);
	WriteRunReg32(XHCI_ERDP_LO(0), (uint32)addr | ERDP_BUSY);
	WriteRunReg32(XHCI_ERDP_HI(0), (uint32)(addr >> 32));
}


int32
XHCI::FinishThread(void* data)
{
	((XHCI *)data)->FinishTransfers();
	return B_OK;
}


void
XHCI::FinishTransfers()
{
	while (!fStopThreads) {
		if (acquire_sem(fFinishTransfersSem) < B_OK)
			continue;

		// eat up sems that have been released by multiple interrupts
		int32 semCount = 0;
		get_sem_count(fFinishTransfersSem, &semCount);
		if (semCount > 0)
			acquire_sem_etc(fFinishTransfersSem, semCount, B_RELATIVE_TIMEOUT, 0);

		mutex_lock(&fFinishedLock);
		TRACE("finishing transfers\n");
		while (!fFinishedList.IsEmpty()) {
			XhciTransferDesc* td = fFinishedList.RemoveHead();
			mutex_unlock(&fFinishedLock);

			TRACE("finishing transfer td %p\n", td);

			UsbBusTransfer* transfer = td->fTransfer;
			if (transfer == NULL) {
				// No transfer? Quick way out.
				delete td;
				mutex_lock(&fFinishedLock);
				continue;
			}

			bool directionIn = (transfer->TransferPipe()->Direction() != UsbBusPipe::Out);

			status_t callbackStatus = B_OK;
			const uint8 completionCode = td->fTrbCompletionCode;
			switch (completionCode) {
				case COMP_SHORT_PACKET:
				case COMP_SUCCESS:
					callbackStatus = B_OK;
					break;
				case COMP_DATA_BUFFER:
					callbackStatus = directionIn ? B_DEV_DATA_OVERRUN
						: B_DEV_DATA_UNDERRUN;
					break;
				case COMP_BABBLE:
					callbackStatus = directionIn ? B_DEV_FIFO_OVERRUN
						: B_DEV_FIFO_UNDERRUN;
					break;
				case COMP_USB_TRANSACTION:
					callbackStatus = B_DEV_CRC_ERROR;
					break;
				case COMP_STALL:
					callbackStatus = B_DEV_STALLED;
					break;
				default:
					callbackStatus = B_DEV_STALLED;
					break;
			}

			size_t actualLength = transfer->FragmentLength();
			if (completionCode != COMP_SUCCESS) {
				actualLength = td->fTdTransferred;
				if (td->fTdTransferred == -1)
					actualLength = transfer->FragmentLength() - td->fTrbLeft;
				TRACE("transfer not successful, actualLength=%" B_PRIuSIZE "\n",
					actualLength);
			}

			usb_isochronous_data* isochronousData = transfer->IsochronousData();
			if (isochronousData != NULL) {
				size_t packetSize = transfer->DataLength()
						/ isochronousData->packet_count,
					left = actualLength;
				for (uint32 i = 0; i < isochronousData->packet_count; i++) {
					size_t size = std::min<size_t>(packetSize, left);
					isochronousData->packet_descriptors[i].actual_length = size;
					isochronousData->packet_descriptors[i].status = (size > 0)
						? B_OK : B_DEV_FIFO_UNDERRUN;
					left -= size;
 				}
 			}

			if (callbackStatus == B_OK && directionIn && actualLength > 0) {
				TRACE("copying in iov count %ld\n", transfer->VectorCount());
				status_t status = transfer->PrepareKernelAccess();
				if (status == B_OK) {
					td->Read(transfer->Vector(),
						transfer->VectorCount(), transfer->IsPhysical());
				} else {
					callbackStatus = status;
				}
			}

			delete td;

			// this transfer may still have data left
			bool finished = true;
			transfer->AdvanceByFragment(actualLength);
			if (completionCode == COMP_SUCCESS
					&& transfer->FragmentLength() > 0) {
				TRACE("still %" B_PRIuSIZE " bytes left on transfer\n",
					transfer->FragmentLength());
				callbackStatus = SubmitTransfer(transfer);
				finished = (callbackStatus != B_OK);
			}
			if (finished) {
				// The actualLength was already handled in AdvanceByFragment.
				transfer->Finished(callbackStatus, 0);
				transfer->Free();
			}

			mutex_lock(&fFinishedLock);
		}
		mutex_unlock(&fFinishedLock);
	}
}


// #pragma mark - Register access

inline void
XHCI::WriteOpReg(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fOperationalRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadOpReg(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fOperationalRegisterOffset + reg);
}


inline status_t
XHCI::WaitOpBits(uint32 reg, uint32 mask, uint32 expected)
{
	int loops = 0;
	uint32 value = ReadOpReg(reg);
	while ((value & mask) != expected) {
		snooze(1000);
		value = ReadOpReg(reg);
		if (loops == 100) {
			TRACE("delay waiting on reg 0x%" B_PRIX32 " match 0x%" B_PRIX32
				" (0x%" B_PRIX32 ")\n",	reg, expected, mask);
		} else if (loops > 250) {
			TRACE_ERROR("timeout waiting on reg 0x%" B_PRIX32
				" match 0x%" B_PRIX32 " (0x%" B_PRIX32 ")\n", reg, expected,
				mask);
			return B_ERROR;
		}
		loops++;
	}
	return B_OK;
}


inline uint32
XHCI::ReadCapReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fCapabilityRegisterOffset + reg);
}


inline void
XHCI::WriteCapReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fCapabilityRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadRunReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fRuntimeRegisterOffset + reg);
}


inline void
XHCI::WriteRunReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fRuntimeRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadDoorReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fDoorbellRegisterOffset + reg);
}


inline void
XHCI::WriteDoorReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fDoorbellRegisterOffset + reg) = value;
}


inline addr_t
XHCI::_OffsetContextAddr(addr_t p)
{
	if (fContextSizeShift == 1) {
		// each structure is page aligned, each pointer is 32 bits aligned
		uint32 offset = p & ((B_PAGE_SIZE - 1) & ~31U);
		p += offset;
	}
	return p;
}

inline uint32
XHCI::_ReadContext(uint32* p)
{
	p = (uint32*)_OffsetContextAddr((addr_t)p);
	return *p;
}


inline void
XHCI::_WriteContext(uint32* p, uint32 value)
{
	p = (uint32*)_OffsetContextAddr((addr_t)p);
	*p = value;
}


inline uint64
XHCI::_ReadContext(uint64* p)
{
	p = (uint64*)_OffsetContextAddr((addr_t)p);
	return *p;
}


inline void
XHCI::_WriteContext(uint64* p, uint64 value)
{
	p = (uint64*)_OffsetContextAddr((addr_t)p);
	*p = value;
}


// #pragma mark -

static driver_module_info sXhciDriverModule = {
	.info = {
		.name = XHCI_DRIVER_MODULE_NAME,
	},
	.probe = XHCI::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sXhciDriverModule,
	NULL
};
