#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Clock.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#include "starfive-jh7110-clkgen.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define CHECK_RET_MSG(err, msg...) \
	{ \
		status_t _err = (err); \
		if (_err < B_OK) { \
			dprintf(msg); \
			return _err; \
		} \
	} \


#define JH7110_CLOCK_DRIVER_MODULE_NAME "drivers/clock/jh7110_clock/driver/v1"


union StarfiveClockRegs {
	struct {
		uint32 div: 24;
		uint32 mux: 6;
		uint32 invert: 1;
		uint32 enable: 1;
	};
	uint32 val;
};


class Jh7110ClockDriver: public DeviceDriver, public ClockController {
private:
	struct MmioRange {
		AreaDeleter area;
		size_t size {};
		StarfiveClockRegs volatile *regs {};

		status_t Init(phys_addr_t physAdr, size_t size);
	};

	class Jh7110ClockDevice: public ClockDevice {
	public:
		Jh7110ClockDevice(Jh7110ClockDriver& base): fBase(base) {}

		int32 Id() const {return this - fBase.GetClock(0);};

		DeviceNode* OwnerNode() final;

		bool IsEnabled() const final;
		status_t SetEnabled(bool doEnable) final;

		int64 GetRate() const final;
		int64 SetRate(int64 rate) final;
		int64 SetRateDry(int64 rate) const final;

		ClockDevice* GetParent() const final;
		status_t SetParent(ClockDevice* parent) final;

	private:
		Jh7110ClockDriver& fBase;
	};

public:
	Jh7110ClockDriver(DeviceNode* node);
	virtual ~Jh7110ClockDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// ClockController
	ClockDevice* GetDevice(const uint8* optInfo, uint32 optInfoSize) final;

private:
	status_t Init();
	StarfiveClockRegs volatile* GetRegs(int32 id) const;
	Jh7110ClockDevice* GetClock(int32 id) {return (Jh7110ClockDevice*)fClocks[id];}

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	MmioRange fSys;
	MmioRange fStg;
	MmioRange fAon;

	ClockDevice* fOscClock {};
	ClockDevice* fGmac1RmiiRefinClock {};
	ClockDevice* fStgApbClock {};
	ClockDevice* fGmac0RmiiRefinClock {};

	char fClocks[JH7110_CLK_END][sizeof(Jh7110ClockDevice)];
};


status_t
Jh7110ClockDriver::MmioRange::Init(phys_addr_t physAdr, size_t size)
{
	area.SetTo(map_physical_memory("StarfiveClock MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&regs));

	CHECK_RET(area.Get());

	return B_OK;
}


// #pragma mark - Jh7110ClockDriver

status_t
Jh7110ClockDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<Jh7110ClockDriver> driver(new(std::nothrow) Jh7110ClockDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


Jh7110ClockDriver::Jh7110ClockDriver(DeviceNode* node): fNode(node)
{
	for (auto clock: fClocks)
		new(clock) Jh7110ClockDevice(*this);
}


Jh7110ClockDriver::~Jh7110ClockDriver()
{
	for (auto clock: fClocks)
		((Jh7110ClockDevice*)clock)->~Jh7110ClockDevice();
}


status_t
Jh7110ClockDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	uint64 regsLen = 0;
	CHECK_RET(fFdtDevice->GetRegByName("sys", &regs, &regsLen));
	CHECK_RET(fSys.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("stg", &regs, &regsLen));
	CHECK_RET(fStg.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("aon", &regs, &regsLen));
	CHECK_RET(fAon.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetClockByName("osc",              &fOscClock));
	CHECK_RET(fFdtDevice->GetClockByName("gmac1_rmii_refin", &fGmac1RmiiRefinClock));
	CHECK_RET(fFdtDevice->GetClockByName("stg_apb",          &fStgApbClock));
	CHECK_RET(fFdtDevice->GetClockByName("gmac0_rmii_refin", &fGmac0RmiiRefinClock));

	return B_OK;
}


StarfiveClockRegs volatile*
Jh7110ClockDriver::GetRegs(int32 id) const
{
	if (id < JH7110_CLK_SYS_REG_END)
		return fSys.regs + id;

	if (id < JH7110_CLK_STG_REG_END)
		return fStg.regs + (id - JH7110_CLK_SYS_REG_END);

	if (id < JH7110_CLK_REG_END)
		return fAon.regs + (id - JH7110_CLK_STG_REG_END);

	return NULL;
}


void*
Jh7110ClockDriver::QueryInterface(const char* name)
{
	if (strcmp(name, ClockController::ifaceName) == 0)
		return static_cast<ClockController*>(this);

	return NULL;
}


ClockDevice*
Jh7110ClockDriver::GetDevice(const uint8* optInfo, uint32 optInfoSize)
{
	if (optInfoSize != 4)
		return NULL;

	uint32 id = B_BENDIAN_TO_HOST_INT32(*(const uint32*)optInfo);
	if (id >= B_COUNT_OF(fClocks))
		return NULL;

	return GetClock(id);
}


// #pragma mark - Jh7110ClockDevice

DeviceNode*
Jh7110ClockDriver::Jh7110ClockDevice::OwnerNode()
{
	fBase.fNode->AcquireReference();
	return fBase.fNode;
}


bool
Jh7110ClockDriver::Jh7110ClockDevice::IsEnabled() const
{
	switch (Id()) {
	case JH7110_AHB0:
	case JH7110_AHB1:
	case JH7110_APB0:
	case JH7110_VOUT_SRC:
	case JH7110_NOC_BUS_CLK_DISP_AXI:
	case JH7110_VOUT_TOP_CLK_VOUT_AHB:
	case JH7110_VOUT_TOP_CLK_VOUT_AXI:
	case JH7110_VOUT_TOP_CLK_HDMITX0_MCLK:
	case JH7110_QSPI_CLK_AHB:
	case JH7110_QSPI_CLK_APB:
	case JH7110_QSPI_CLK_REF:
	case JH7110_SDIO0_CLK_AHB:
	case JH7110_SDIO1_CLK_AHB:
	case JH7110_SDIO0_CLK_SDCARD:
	case JH7110_SDIO1_CLK_SDCARD:
	case JH7110_NOC_BUS_CLK_STG_AXI:
	case JH7110_GMAC5_CLK_AHB:
	case JH7110_GMAC5_CLK_AXI:
	case JH7110_GMAC5_CLK_PTP:
	case JH7110_GMAC5_CLK_TX:
	case JH7110_GMAC1_GTXC:
	case JH7110_GMAC0_GTXCLK:
	case JH7110_GMAC0_PTP:
	case JH7110_GMAC0_GTXC:
	case JH7110_I2C2_CLK_APB:
	case JH7110_I2C5_CLK_APB:
	case JH7110_UART0_CLK_APB:
	case JH7110_UART0_CLK_CORE:
	case JH7110_UART1_CLK_APB:
	case JH7110_UART1_CLK_CORE:
	case JH7110_UART2_CLK_APB:
	case JH7110_UART2_CLK_CORE:
	case JH7110_UART3_CLK_APB:
	case JH7110_UART3_CLK_CORE:
	case JH7110_UART4_CLK_APB:
	case JH7110_UART4_CLK_CORE:
	case JH7110_UART5_CLK_APB:
	case JH7110_UART5_CLK_CORE:
	case JH7110_I2STX_4CH0_BCLK_MST:
	case JH7110_USB0_CLK_USB_APB:
	case JH7110_USB0_CLK_UTMI_APB:
	case JH7110_USB0_CLK_AXI:
	case JH7110_USB0_CLK_LPM:
	case JH7110_USB0_CLK_STB:
	case JH7110_USB0_CLK_APP_125:
	case JH7110_PCIE0_CLK_AXI_MST0:
	case JH7110_PCIE0_CLK_APB:
	case JH7110_PCIE0_CLK_TL:
	case JH7110_PCIE1_CLK_AXI_MST0:
	case JH7110_PCIE1_CLK_APB:
	case JH7110_PCIE1_CLK_TL:
	case JH7110_U0_GMAC5_CLK_AHB:
	case JH7110_U0_GMAC5_CLK_AXI:
	case JH7110_U0_GMAC5_CLK_TX:
	case JH7110_OTPC_CLK_APB:
	case JH7110_I2C5_CLK_CORE:
	case JH7110_U0_DC8200_CLK_PIX0:
	case JH7110_U0_DC8200_CLK_PIX1:
	case JH7110_DOM_VOUT_TOP_LCD_CLK:
	case JH7110_U0_CDNS_DSITX_CLK_DPI:
	case JH7110_U0_DC8200_CLK_AXI:
	case JH7110_U0_DC8200_CLK_CORE:
	case JH7110_U0_DC8200_CLK_AHB:
	case JH7110_U0_MIPITX_DPHY_CLK_TXESC:
	case JH7110_U0_CDNS_DSITX_CLK_SYS:
	case JH7110_U0_CDNS_DSITX_CLK_APB:
	case JH7110_U0_CDNS_DSITX_CLK_TXESC:
	case JH7110_U0_HDMI_TX_CLK_SYS:
	case JH7110_U0_HDMI_TX_CLK_MCLK:
	case JH7110_U0_HDMI_TX_CLK_BCLK:
		return fBase.GetRegs(Id())->enable;
	}
	return true;
}


status_t
Jh7110ClockDriver::Jh7110ClockDevice::SetEnabled(bool doEnable)
{
	switch (Id()) {
		case JH7110_AHB0:
		case JH7110_AHB1:
		case JH7110_APB0:
		case JH7110_VOUT_SRC:
		case JH7110_NOC_BUS_CLK_DISP_AXI:
		case JH7110_VOUT_TOP_CLK_VOUT_AHB:
		case JH7110_VOUT_TOP_CLK_VOUT_AXI:
		case JH7110_VOUT_TOP_CLK_HDMITX0_MCLK:
		case JH7110_QSPI_CLK_AHB:
		case JH7110_QSPI_CLK_APB:
		case JH7110_QSPI_CLK_REF:
		case JH7110_SDIO0_CLK_AHB:
		case JH7110_SDIO1_CLK_AHB:
		case JH7110_SDIO0_CLK_SDCARD:
		case JH7110_SDIO1_CLK_SDCARD:
		case JH7110_NOC_BUS_CLK_STG_AXI:
		case JH7110_GMAC5_CLK_AHB:
		case JH7110_GMAC5_CLK_AXI:
		case JH7110_GMAC5_CLK_PTP:
		case JH7110_GMAC5_CLK_TX:
		case JH7110_GMAC1_GTXC:
		case JH7110_GMAC0_GTXCLK:
		case JH7110_GMAC0_PTP:
		case JH7110_GMAC0_GTXC:
		case JH7110_I2C2_CLK_APB:
		case JH7110_I2C5_CLK_APB:
		case JH7110_UART0_CLK_APB:
		case JH7110_UART0_CLK_CORE:
		case JH7110_UART1_CLK_APB:
		case JH7110_UART1_CLK_CORE:
		case JH7110_UART2_CLK_APB:
		case JH7110_UART2_CLK_CORE:
		case JH7110_UART3_CLK_APB:
		case JH7110_UART3_CLK_CORE:
		case JH7110_UART4_CLK_APB:
		case JH7110_UART4_CLK_CORE:
		case JH7110_UART5_CLK_APB:
		case JH7110_UART5_CLK_CORE:
		case JH7110_I2STX_4CH0_BCLK_MST:
		case JH7110_USB0_CLK_USB_APB:
		case JH7110_USB0_CLK_UTMI_APB:
		case JH7110_USB0_CLK_AXI:
		case JH7110_USB0_CLK_LPM:
		case JH7110_USB0_CLK_STB:
		case JH7110_USB0_CLK_APP_125:
		case JH7110_PCIE0_CLK_AXI_MST0:
		case JH7110_PCIE0_CLK_APB:
		case JH7110_PCIE0_CLK_TL:
		case JH7110_PCIE1_CLK_AXI_MST0:
		case JH7110_PCIE1_CLK_APB:
		case JH7110_PCIE1_CLK_TL:
		case JH7110_U0_GMAC5_CLK_AHB:
		case JH7110_U0_GMAC5_CLK_AXI:
		case JH7110_U0_GMAC5_CLK_TX:
		case JH7110_OTPC_CLK_APB:
		case JH7110_I2C5_CLK_CORE:
		case JH7110_U0_DC8200_CLK_PIX0:
		case JH7110_U0_DC8200_CLK_PIX1:
		case JH7110_DOM_VOUT_TOP_LCD_CLK:
		case JH7110_U0_CDNS_DSITX_CLK_DPI:
		case JH7110_U0_DC8200_CLK_AXI:
		case JH7110_U0_DC8200_CLK_CORE:
		case JH7110_U0_DC8200_CLK_AHB:
		case JH7110_U0_MIPITX_DPHY_CLK_TXESC:
		case JH7110_U0_CDNS_DSITX_CLK_SYS:
		case JH7110_U0_CDNS_DSITX_CLK_APB:
		case JH7110_U0_CDNS_DSITX_CLK_TXESC:
		case JH7110_U0_HDMI_TX_CLK_SYS:
		case JH7110_U0_HDMI_TX_CLK_MCLK:
		case JH7110_U0_HDMI_TX_CLK_BCLK:
			fBase.GetRegs(Id())->enable = doEnable;
			break;
	}
	return B_OK;
}


int64
Jh7110ClockDriver::Jh7110ClockDevice::GetRate() const
{
	int32 id = Id();
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	auto GetDivider = [regs](ClockDevice* parent, uint32 divWidth) -> int64 {
		int64 parentRate = parent->GetRate();
		if (parentRate < B_OK)
			return parentRate;

		uint32 div = regs->div & ((1 << divWidth) - 1);
		if (div == 0)
			return B_BAD_VALUE;

		return (parentRate + div - 1) / div;
	};

	auto GetMux = [regs](ClockDevice* parent1, ClockDevice* parent2) -> int64 {
		switch (regs->mux) {
			case 0:
				return parent1->GetRate();
			case 1:
				return parent2->GetRate();
			default:
				return B_BAD_VALUE;
		}
	};

	switch (id) {
		case JH7110_PLL0_OUT:
			return 1250000000;
		case JH7110_PLL1_OUT:
			return 1066000000;
		case JH7110_PLL2_OUT:
			return 1228800000;
		case JH7110_SDIO0_CLK_AHB:
		case JH7110_SDIO1_CLK_AHB:
			return fBase.GetClock(JH7110_AHB0)->GetRate();
		case JH7110_SDIO0_CLK_SDCARD:
		case JH7110_SDIO1_CLK_SDCARD:
			return GetDivider(fBase.GetClock(JH7110_AXI_CFG0), 4);
		case JH7110_AHB0:
			return fBase.GetClock(JH7110_STG_AXIAHB)->GetRate();
		case JH7110_STG_AXIAHB:
			return GetDivider(fBase.GetClock(JH7110_AXI_CFG0), 2);
		case JH7110_AXI_CFG0:
			return GetDivider(fBase.GetClock(JH7110_BUS_ROOT), 2);
		case JH7110_BUS_ROOT:
			return GetMux(fBase.fOscClock, fBase.GetClock(JH7110_PLL2_OUT));
	}
	return ENOSYS;
}


int64
Jh7110ClockDriver::Jh7110ClockDevice::SetRate(int64 rate)
{
	return ENOSYS;
}


int64
Jh7110ClockDriver::Jh7110ClockDevice::SetRateDry(int64 rate) const
{
	return ENOSYS;
}


ClockDevice*
Jh7110ClockDriver::Jh7110ClockDevice::GetParent() const
{
	int32 id = Id();
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	switch (id) {
		case JH7110_CPU_ROOT:
			switch (regs->mux) {
				case 0:
					return fBase.fOscClock;
				case 1:
					return fBase.GetClock(JH7110_PLL0_OUT);
				default:
					break;
			}
		case JH7110_PERH_ROOT:
			switch (regs->mux) {
				case 0:
					return fBase.GetClock(JH7110_PLL0_OUT);
				case 1:
					return fBase.GetClock(JH7110_PLL2_OUT);
				default:
					break;
			}
			break;
		case JH7110_BUS_ROOT:
			switch (regs->mux) {
				case 0:
					return fBase.fOscClock;
				case 1:
					return fBase.GetClock(JH7110_PLL2_OUT);
				default:
					break;
			}
			break;
	}
	return NULL;
}


status_t
Jh7110ClockDriver::Jh7110ClockDevice::SetParent(ClockDevice* parent)
{
	int32 id = Id();
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	switch (id) {
		case JH7110_CPU_ROOT:
			if (parent == fBase.fOscClock)
				regs->mux = 0;
			else if (parent == fBase.GetClock(JH7110_PLL0_OUT))
				regs->mux = 1;
			break;
		case JH7110_PERH_ROOT:
			if (parent == fBase.GetClock(JH7110_PLL0_OUT))
				regs->mux = 0;
			else if (parent == fBase.GetClock(JH7110_PLL2_OUT))
				regs->mux = 1;
			break;
		case JH7110_BUS_ROOT:
			if (parent == fBase.fOscClock)
				regs->mux = 0;
			else if (parent == fBase.GetClock(JH7110_PLL2_OUT))
				regs->mux = 1;
			break;
	}
	return B_BAD_VALUE;
}


static driver_module_info sJh7110ClockDriverModule = {
	.info = {
		.name = JH7110_CLOCK_DRIVER_MODULE_NAME,
	},
	.probe = Jh7110ClockDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sJh7110ClockDriverModule,
	NULL
};
