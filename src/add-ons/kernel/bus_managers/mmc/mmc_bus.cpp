#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/bus/MMC.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <util/AutoLock.h>

#include <lock.h>


#define MMCBUS_TRACE
#ifdef MMCBUS_TRACE
#	define TRACE(x...)		dprintf("\33[33mmmc_bus:\33[0m " x)
#else
#	define TRACE(x...)
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mmmc_bus:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33mmmc_bus:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define MMC_BUS_DRIVER_MODULE_NAME "bus_managers/mmc/driver/v1"


class MmcBusDriver;


class MmcDeviceImpl: public BusDriver, public MmcDevice {
public:
	MmcDeviceImpl(MmcBusDriver& base, uint16 rca): fBase(base), fRca(rca) {}

	// BusDriver
	void* QueryInterface(const char* name) final;

	// MmcDevice
	status_t ExecuteCommand(uint8 command, uint32 argument, uint32* result) final;
	status_t DoIO(uint8 command, IOOperation* operation, bool offsetAsSectors) final;
	status_t SetBusWidth(int width) final;

private:
	MmcBusDriver& fBase;
	uint16 fRca;
};


class MmcBusDriver: public DeviceDriver {
public:
	MmcBusDriver(DeviceNode* node): fNode(node) {}
	virtual ~MmcBusDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	inline MmcBus* GetMmcBus() {return fMmcBus;}
	inline mutex* GetBusLock() {return &fBusLock;}
	status_t ActivateDevice(uint16 rca);

private:
	status_t Init();

private:
	DeviceNode* fNode;
	mutex fBusLock = MUTEX_INITIALIZER("MMC Bus");
	MmcBus* fMmcBus {};
	uint16 fActiveDevice {};
};


// #pragma mark - MmcBusDriver

status_t
MmcBusDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<MmcBusDriver> driver(new(std::nothrow) MmcBusDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
MmcBusDriver::Init()
{
	dprintf("MmcBusDriver::Init()\n");
	fMmcBus = fNode->QueryBusInterface<MmcBus>();

	TRACE("Reset the bus...\n");
	status_t result = fMmcBus->ExecuteCommand(SD_GO_IDLE_STATE, 0, NULL);
	TRACE("CMD0 result: %s\n", strerror(result));

	CHECK_RET(result);

	// Need to wait at least 8 clock cycles after CMD0 before sending the next
	// command. With the default 400kHz clock that would be 20 microseconds,
	// but we need to wait at least 20ms here, otherwise the next command times
	// out
	snooze(30000);

	TRACE("Scanning the bus\n");

	// Use the low speed clock and 1bit bus width for scanning
	fMmcBus->SetClock(400);
	fMmcBus->SetBusWidth(1);

	// Probe the voltage range
	enum {
		// Table 4-40 in physical layer specification v8.00
		// All other values are currently reserved
		HOST_27_36V = 1, //Host supplied voltage 2.7-3.6V
	};

	// An arbitrary value, we just need to check that the response
	// containts the same.
	static const uint8 kVoltageCheckPattern = 0xAA;

	// FIXME MMC cards will not reply to this! They expect CMD1 instead
	// SD v1 cards will also not reply, but we can proceed to ACMD41
	// If ACMD41 also does not work, it may be an SDIO card, too
	uint32_t probe = (HOST_27_36V << 8) | kVoltageCheckPattern;
	uint32_t hcs = 1 << 30;
	uint32_t response;
	if (fMmcBus->ExecuteCommand(SD_SEND_IF_COND, probe, &response) != B_OK) {
		TRACE("Card does not implement CMD8, may be a V1 SD card\n");
		// Do not check for SDHC support in this case
		hcs = 0;
	} else if (response != probe) {
		ERROR("Card does not support voltage range (expected %x, "
			"reply %x)\n", probe, response);
		// TODO we should power off the bus in this case.
	}

	// Probe OCR, waiting for card to become ready
	// We keep repeating ACMD41 until the card replies that it is
	// initialized.
	uint32_t ocr = 0;
	if (hcs != 0) {
		do {
			uint32_t cardStatus {};
			while (fMmcBus->ExecuteCommand(SD_APP_CMD, 0, &cardStatus) == B_TIMED_OUT) {
				ERROR("Card locked after CMD8...\n");
				snooze(1000000);
			}
			if ((cardStatus & 0xFFFF8000) != 0)
				ERROR("SD card reports error %x\n", cardStatus);
			if ((cardStatus & (1 << 5)) == 0)
				ERROR("Card did not enter ACMD mode\n");

			fMmcBus->ExecuteCommand(SD_SEND_OP_COND, hcs | 0xFF8000, &ocr);

			if ((ocr & (1 << 31)) == 0) {
				TRACE("Card is busy\n");
				snooze(100000);
			}
		} while (((ocr & (1 << 31)) == 0));
	}

	// FIXME this should be asked to each card, when there are multiple
	// ones. So ACMD41 should be moved inside the probing loop below?
	uint8_t cardType = CARD_TYPE_SD;

	if ((ocr & hcs) != 0)
		cardType = CARD_TYPE_SDHC;
	if ((ocr & (1 << 29)) != 0)
		cardType = CARD_TYPE_UHS2;
	if ((ocr & (1 << 24)) != 0)
		TRACE("Card supports 1.8v");
	TRACE("Voltage range: %x\n", ocr & 0xFFFFFF);

	// TODO send CMD11 to switch to low voltage mode if card supports it?

	// We use CMD2 (ALL_SEND_CID) and CMD3 (SEND_RELATIVE_ADDR) to assign
	// an RCA to all cards. Initially all cards have an RCA of 0 and will
	// all receive CMD2. But only ne of them will reply (they do collision
	// detection while sending the CID in reply). We assign a new RCA to
	// that first card, and repeat the process with the remaining ones
	// until no one answers to CMD2. Then we know all cards have an RCA
	// (and a matching published device on our side).
	uint32_t cid[4];

	while (fMmcBus->ExecuteCommand(SD_ALL_SEND_CID, 0, cid) == B_OK) {
		fMmcBus->ExecuteCommand(SD_SEND_RELATIVE_ADDR, 0, &response);

		TRACE("RCA: %x Status: %x\n", response >> 16, response & 0xFFFF);

		if ((response & 0xFF00) != 0x500) {
			TRACE("Card did not enter data state\n");
			// This probably means there are no more cards to scan on the
			// bus, so exit the loop.
			break;
		}

		// The card now has an RCA and it entered the data phase, which
		// means our initializing job is over, we can pass it on to the
		// mmc_disk driver.

		uint32_t vendor = cid[3] & 0xFFFFFF;
		char name[6] = {(char)(cid[2] >> 24), (char)(cid[2] >> 16),
			(char)(cid[2] >> 8), (char)cid[2], (char)(cid[1] >> 24), 0};
		uint32_t serial = (cid[1] << 16) | (cid[0] >> 16);
		uint16_t revision = (cid[1] >> 20) & 0xF;
		revision *= 100;
		revision += (cid[1] >> 16) & 0xF;
		uint8_t month = cid[0] & 0xF;
		uint16_t year = 2000 + ((cid[0] >> 4) & 0xFF);
		uint16_t rca = response >> 16;

		TRACE("vendor: %#" B_PRIx32 "\n", vendor);
		TRACE("name: \"%s\"\n", name);
		TRACE("serial: %#" B_PRIx32 "\n", serial);
		TRACE("revision: %#" B_PRIx16 "\n", revision);

		device_attr attrs[] = {
			{ B_DEVICE_BUS, B_STRING_TYPE, {.string = "mmc" }},
			{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "mmc device" }},
			{ "mmc/vendor", B_UINT32_TYPE, {.ui32 = vendor}},
			{ "mmc/id", B_STRING_TYPE, {.string = name}},
			{ B_DEVICE_UNIQUE_ID, B_UINT32_TYPE, {.ui32 = serial}},
			{ "mmc/revision", B_UINT16_TYPE, {.ui16 = revision}},
			{ "mmc/month", B_UINT8_TYPE, {.ui8 = month}},
			{ "mmc/year", B_UINT16_TYPE, {.ui16 = year}},
			{ MMC_DEVICE_RCA, B_UINT16_TYPE, {.ui16 = rca}},
			{ MMC_DEVICE_TYPE, B_UINT8_TYPE, {.ui8 = cardType}},
			{}
		};

		// publish child device for the card
		fNode->RegisterNode(this, new(std::nothrow) MmcDeviceImpl(*this, rca), attrs, NULL);
	}

	// TODO if there is a single card active, check if it supports CMD6
	// (spec version 1.10 or later in SCR). If it does, check if CMD6 can
	// enable high speed mode, use that to go to 50MHz instead of 25.
	fMmcBus->SetClock(25000);

	// FIXME we also need to unpublish devices that are gone. Probably need
	// to "ping" all RCAs somehow? Or is there an interrupt we can look for
	// to detect added/removed cards?

	return B_OK;
}


status_t
MmcBusDriver::ActivateDevice(uint16 rca)
{
	// Do nothing if the device is already activated
	if (fActiveDevice == rca)
		return B_OK;

	uint32_t response;
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SELECT_DESELECT_CARD, ((uint32)rca) << 16, &response));

	fActiveDevice = rca;

	return B_OK;
}


// #pragma mark - MmcDeviceImpl

void*
MmcDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, MmcDevice::ifaceName) == 0)
		return static_cast<MmcDevice*>(this);

	return NULL;
}


status_t
MmcDeviceImpl::ExecuteCommand(uint8 command, uint32 argument, uint32* result)
{
	MutexLocker lock(fBase.GetBusLock());
	CHECK_RET(fBase.ActivateDevice(fRca));
	return fBase.GetMmcBus()->ExecuteCommand(command, argument, result);
}


status_t
MmcDeviceImpl::DoIO(uint8 command, IOOperation* operation, bool offsetAsSectors)
{
	MutexLocker lock(fBase.GetBusLock());
	CHECK_RET(fBase.ActivateDevice(fRca));
	return fBase.GetMmcBus()->DoIO(command, operation, offsetAsSectors);
}


status_t
MmcDeviceImpl::SetBusWidth(int width)
{
	return fBase.GetMmcBus()->SetBusWidth(width);
}


static driver_module_info sMmcBusDriverModule = {
	.info = {
		.name = MMC_BUS_DRIVER_MODULE_NAME,
	},
	.probe = MmcBusDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sMmcBusDriverModule,
	NULL
};
