#include <string.h>
#include <algorithm>
#include <variant>
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


class Jh7110ClockDriver;

typedef ClockDevice* (*GetParentFn)(Jh7110ClockDriver& base, uint32 index);

union StarfiveClockRegs {
	struct {
		uint32 div: 24;
		uint32 mux: 6;
		uint32 invert: 1;
		uint32 enable: 1;
	};
	uint32 val;
};


enum class ClockDefType: uint8 {
	empty,
	composite,
	fixed,
	fixFactor
};

struct ClockDefComposite {
	ClockDefType type = ClockDefType::composite;
	uint8 gate;
	uint8 div;
	uint8 mux = 1;
	uint16 parents[2];
};

struct ClockDefFixed {
	ClockDefType type = ClockDefType::fixed;
	uint32 rate;
};

struct ClockDefFixFactor {
	ClockDefType type = ClockDefType::fixFactor;
	uint32 mul;
	uint32 div;
	uint16 parent;
};

union ClockDef {
	ClockDefType type;
	ClockDefComposite composite;
	ClockDefFixed fixed;
	ClockDefFixFactor fixFactor;
};


class Jh7110ClockDriver: public DeviceDriver, public ClockController {
private:
	struct MmioRange {
		AreaDeleter area;
		size_t size {};
		StarfiveClockRegs volatile *regs {};

		status_t Init(phys_addr_t physAdr, size_t size);
	};

	class ClockDevice: public ::ClockDevice {
	public:
		virtual ~ClockDevice() = default;
		ClockDevice(Jh7110ClockDriver& base): fBase(base) {}

		int32 Id() const {return this - (ClockDevice*)fBase.fClocks[0];};

		DeviceNode* OwnerNode() final;

		bool IsEnabled() const final;
		status_t SetEnabled(bool doEnable) final;

		int64 GetRate() const final;
		int64 SetRate(int64 rate) final;
		int64 SetRateDry(int64 rate) const final;

		::ClockDevice* GetParent() const final;
		status_t SetParent(::ClockDevice* parent) final;

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
	::ClockDevice* GetDevice(const uint8* optInfo, uint32 optInfoSize) final;

private:
	status_t Init();
	StarfiveClockRegs volatile* GetRegs(int32 id) const;
	::ClockDevice* GetClock(int32 id) {return (id < JH7110_CLK_END) ? (ClockDevice*)fClocks[id] : fExternalClocks[id - JH7110_CLK_END];}

	ClockDef GetClockDef(int32 id);

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	MmioRange fSys;
	MmioRange fStg;
	MmioRange fAon;

	char fClocks[JH7110_CLK_END][sizeof(ClockDevice)] {};
	::ClockDevice* fExternalClocks[15] {};
};


static uint64
div_round_up(uint64 value, uint64 div)
{
	return (value + div - 1) / div;
}


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
	for (char* clock: fClocks) {
		new(clock) ClockDevice(*this);
	}
}


Jh7110ClockDriver::~Jh7110ClockDriver()
{
	for (char* clock: fClocks)
		((ClockDevice*)clock)->~ClockDevice();
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

	CHECK_RET(fFdtDevice->GetClockByName("osc",              &fExternalClocks[JH7110_OSC              - JH7110_CLK_END]));
	CHECK_RET(fFdtDevice->GetClockByName("gmac1_rmii_refin", &fExternalClocks[JH7110_GMAC1_RMII_REFIN - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("gmac1_rgmii_rxin", &fExternalClocks[JH7110_GMAC1_RGMII_RXIN - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("i2stx_bclk_ext",   &fExternalClocks[JH7110_I2STX_BCLK_EXT   - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("i2stx_lrck_ext",   &fExternalClocks[JH7110_I2STX_LRCK_EXT   - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("i2srx_bclk_ext",   &fExternalClocks[JH7110_I2SRX_BCLK_EXT   - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("i2srx_lrck_ext",   &fExternalClocks[JH7110_I2SRX_LRCK_EXT   - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("tdm_ext",          &fExternalClocks[JH7110_TDM_EXT          - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("mclk_ext",         &fExternalClocks[JH7110_MCLK_EXT         - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("jtag_tck_inner",   &fExternalClocks[JH7110_JTAG_TCK_INNER   - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("bist_apb",         &fExternalClocks[JH7110_BIST_APB         - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("stg_apb",          &fExternalClocks[JH7110_STG_APB          - JH7110_CLK_END]));
	CHECK_RET(fFdtDevice->GetClockByName("gmac0_rmii_refin", &fExternalClocks[JH7110_GMAC0_RMII_REFIN - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("gmac0_rgmii_rxin", &fExternalClocks[JH7110_GMAC0_RGMII_RXIN - JH7110_CLK_END]));
//	CHECK_RET(fFdtDevice->GetClockByName("clk_rtc",          &fExternalClocks[JH7110_CLK_RTC          - JH7110_CLK_END]));

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


ClockDef
Jh7110ClockDriver::GetClockDef(int32 id)
{
	switch (id) {
		case JH7110_CPU_ROOT: return {.composite = {.mux = 1, .parents = {JH7110_OSC, JH7110_PLL0_OUT}}};
		case JH7110_CPU_CORE: return {.composite = {.div = 3, .parents = {JH7110_CPU_ROOT}}};
		case JH7110_CPU_BUS: return {.composite = {.div = 2, .parents = {JH7110_CPU_CORE}}};
		case JH7110_PERH_ROOT: return {.composite = {.div = 2, .mux = 1, .parents = {JH7110_PLL0_OUT, JH7110_PLL2_OUT}}};
		case JH7110_BUS_ROOT: return {.composite = {.mux = 1, .parents = {JH7110_OSC, JH7110_PLL2_OUT}}};
		case JH7110_NOCSTG_BUS: return {.composite = {.div = 3, .parents = {JH7110_BUS_ROOT}}};
		case JH7110_AXI_CFG0: return {.composite = {.div = 2, .parents = {JH7110_BUS_ROOT}}};
		case JH7110_STG_AXIAHB: return {.composite = {.div = 2, .parents = {JH7110_AXI_CFG0}}};
		case JH7110_AHB0: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_AHB1: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_APB_BUS_FUNC: return {.composite = {.div = 4, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_APB0: return {.composite = {.gate = 1, .parents = {JH7110_APB_BUS}}};
		case JH7110_AUDIO_ROOT: return {.composite = {.div = 5, .parents = {JH7110_PLL2_OUT}}};
		case JH7110_MCLK_INNER: return {.composite = {.div = 5, .parents = {JH7110_AUDIO_ROOT}}};
		case JH7110_MCLK: return {.composite = {.mux = 1, .parents = {JH7110_MCLK_INNER, JH7110_MCLK_EXT}}};
		case JH7110_VOUT_SRC: return {.composite = {.gate = 1, .parents = {JH7110_VOUT_ROOT}}};
		case JH7110_VOUT_AXI: return {.composite = {.div = 3, .parents = {JH7110_VOUT_ROOT}}};
		case JH7110_NOC_BUS_CLK_DISP_AXI: return {.composite = {.gate = 1, .parents = {JH7110_VOUT_AXI}}};
		case JH7110_VOUT_TOP_CLK_VOUT_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AHB1}}};
		case JH7110_VOUT_TOP_CLK_VOUT_AXI: return {.composite = {.gate = 1, .parents = {JH7110_VOUT_AXI}}};
		case JH7110_VOUT_TOP_CLK_HDMITX0_MCLK: return {.composite = {.gate = 1, .parents = {JH7110_MCLK}}};
		case JH7110_VOUT_TOP_CLK_MIPIPHY_REF: return {.composite = {.div = 2, .parents = {JH7110_OSC}}};
		case JH7110_QSPI_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AHB1}}};
		case JH7110_QSPI_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB12}}};
		case JH7110_QSPI_REF_SRC: return {.composite = {.div = 5, .parents = {JH7110_GMACUSB_ROOT}}};
//		case JH7110_QSPI_CLK_REF: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_OSC, JH7110_U0_CDNS_QSPI_REF_SRC}}};
		case JH7110_SDIO0_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AHB0}}};
		case JH7110_SDIO1_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AHB0}}};
		case JH7110_SDIO0_CLK_SDCARD: return {.composite = {.gate = 1, .div = 4, .parents = {JH7110_AXI_CFG0}}};
		case JH7110_SDIO1_CLK_SDCARD: return {.composite = {.gate = 1, .div = 4, .parents = {JH7110_AXI_CFG0}}};
		case JH7110_USB_125M: return {.composite = {.div = 4, .parents = {JH7110_GMACUSB_ROOT}}};
		case JH7110_NOC_BUS_CLK_STG_AXI: return {.composite = {.gate = 1, .parents = {JH7110_NOCSTG_BUS}}};
		case JH7110_GMAC5_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AHB0}}};
		case JH7110_GMAC5_CLK_AXI: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_GMAC_SRC: return {.composite = {.div = 3, .parents = {JH7110_GMACUSB_ROOT}}};
		case JH7110_GMAC1_GTXCLK: return {.composite = {.div = 4, .parents = {JH7110_GMACUSB_ROOT}}};
		case JH7110_GMAC1_RMII_RTX: return {.composite = {.div = 5, .parents = {JH7110_GMAC1_RMII_REFIN}}};
		case JH7110_GMAC5_CLK_PTP: return {.composite = {.gate = 1, .div = 5, .parents = {JH7110_GMAC_SRC}}};
		case JH7110_GMAC5_CLK_TX: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_GMAC1_GTXCLK, JH7110_GMAC1_RMII_RTX}}};
		case JH7110_GMAC1_GTXC: return {.composite = {.gate = 1, .parents = {JH7110_GMAC1_GTXCLK}}};
		case JH7110_GMAC0_GTXCLK: return {.composite = {.gate = 1, .div = 4, .parents = {JH7110_GMACUSB_ROOT}}};
		case JH7110_GMAC0_PTP: return {.composite = {.gate = 1, .div = 5, .parents = {JH7110_GMAC_SRC}}};
		case JH7110_GMAC0_GTXC: return {.composite = {.gate = 1, .parents = {JH7110_GMAC0_GTXCLK}}};
		case JH7110_I2C2_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_I2C5_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART0_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART0_CLK_CORE: return {.composite = {.gate = 1, .parents = {JH7110_OSC}}};
		case JH7110_UART1_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART1_CLK_CORE: return {.composite = {.gate = 1, .parents = {JH7110_OSC}}};
		case JH7110_UART2_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART2_CLK_CORE: return {.composite = {.gate = 1, .parents = {JH7110_OSC}}};
		case JH7110_UART3_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART3_CLK_CORE: return {.composite = {.gate = 1, .div = 8, .parents = {JH7110_PERH_ROOT}}};
		case JH7110_UART4_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART4_CLK_CORE: return {.composite = {.gate = 1, .div = 8, .parents = {JH7110_PERH_ROOT}}};
		case JH7110_UART5_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_APB0}}};
		case JH7110_UART5_CLK_CORE: return {.composite = {.gate = 1, .div = 8, .parents = {JH7110_PERH_ROOT}}};
		case JH7110_I2STX_4CH0_BCLK_MST: return {.composite = {.gate = 1, .div = 5, .parents = {JH7110_MCLK}}};
		case JH7110_I2STX0_4CHBCLK: return {.composite = {.mux = 1, .parents = {JH7110_I2STX_4CH0_BCLK_MST, JH7110_I2STX_BCLK_EXT}}};
		case JH7110_USB0_CLK_USB_APB: return {.composite = {.gate = 1, .parents = {JH7110_STG_APB}}};
		case JH7110_USB0_CLK_UTMI_APB: return {.composite = {.gate = 1, .parents = {JH7110_STG_APB}}};
		case JH7110_USB0_CLK_AXI: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_USB0_CLK_LPM: return {.composite = {.gate = 1, .div = 2, .parents = {JH7110_OSC}}};
		case JH7110_USB0_CLK_STB: return {.composite = {.gate = 1, .div = 3, .parents = {JH7110_OSC}}};
		case JH7110_USB0_CLK_APP_125: return {.composite = {.gate = 1, .parents = {JH7110_USB_125M}}};
		case JH7110_USB0_REFCLK: return {.composite = {.div = 2, .parents = {JH7110_OSC}}};
		case JH7110_PCIE0_CLK_AXI_MST0: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_PCIE0_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_STG_APB}}};
		case JH7110_PCIE0_CLK_TL: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_PCIE1_CLK_AXI_MST0: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_PCIE1_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_STG_APB}}};
		case JH7110_PCIE1_CLK_TL: return {.composite = {.gate = 1, .parents = {JH7110_STG_AXIAHB}}};
		case JH7110_U0_GMAC5_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_AON_AHB}}};
		case JH7110_U0_GMAC5_CLK_AXI: return {.composite = {.gate = 1, .parents = {JH7110_AON_AHB}}};
		case JH7110_GMAC0_RMII_RTX: return {.composite = {.div = 5, .parents = {JH7110_GMAC0_RMII_REFIN}}};
		case JH7110_U0_GMAC5_CLK_TX: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_GMAC0_GTXCLK, JH7110_GMAC0_RMII_RTX}}};
		case JH7110_OTPC_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_AON_APB}}};
		case JH7110_PLL0_OUT: return {.fixed = {.rate = 1250000000}};
		case JH7110_PLL1_OUT: return {.fixed = {.rate = 1066000000}};
		case JH7110_PLL2_OUT: return {.fixed = {.rate = 1228800000}};
		case JH7110_AON_APB: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_APB_BUS_FUNC}};
		case JH7110_DDR_ROOT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_PLL1_OUT}};
		case JH7110_VOUT_ROOT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_PLL2_OUT}};
		case JH7110_GMACUSB_ROOT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_PLL0_OUT}};
		case JH7110_PCLK2_MUX_FUNC_PCLK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_APB_BUS_FUNC}};
		case JH7110_APB_BUS: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U2_PCLK_MUX_PCLK}};
		case JH7110_APB12: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_APB_BUS}};
//		case JH7110_VOUT_TOP_CLK_HDMITX0_BCLK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_I2STX_4CH_BCLK}};
		case JH7110_AON_AHB: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_STG_AXIAHB}};
//		case JH7110_I2C2_CLK_CORE: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U2_DW_I2C_CLK_APB}};
		case JH7110_I2C5_CLK_CORE: return {.composite = {.gate = 1, .parents = {JH7110_OSC}}};
//		case JH7110_U2_PCLK_MUX_PCLK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U2_PCLK_MUX_FUNC_PCLK}};
		case JH7110_U0_GMAC5_CLK_PTP: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_GMAC0_PTP}};
//		case JH7110_U0_DC8200_CLK_PIX0: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_U0_DC8200_CLK_PIX_SELS}}};
//		case JH7110_U0_DC8200_CLK_PIX1: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_U0_DC8200_CLK_PIX_SELS}}};
//		case JH7110_DOM_VOUT_TOP_LCD_CLK: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_DOM_VOUT_TOP_LCD_CLK_SELS}}};
//		case JH7110_U0_CDNS_DSITX_CLK_DPI: return {.composite = {.gate = 1, .mux = 1, .parents = {JH7110_U0_CDNS_DSITX_CLK_SELS}}};
		case JH7110_APB: return {.composite = {.div = 5, .parents = {JH7110_DISP_AHB}}};
		case JH7110_TX_ESC: return {.composite = {.div = 5, .parents = {JH7110_DISP_AHB}}};
		case JH7110_DC8200_PIX0: return {.composite = {.div = 6, .parents = {JH7110_DISP_ROOT}}};
		case JH7110_DSI_SYS: return {.composite = {.div = 5, .parents = {JH7110_DISP_ROOT}}};
		case JH7110_STG_APB: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_APB_BUS}};
//		case JH7110_DISP_ROOT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DOM_VOUT_TOP_CLK_VOUT_SRC}};
//		case JH7110_DISP_AXI: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DOM_VOUT_TOP_VOUT_AXI}};
//		case JH7110_DISP_AHB: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DOM_VOUT_TOP_CLK_VOUT_AHB}};
//		case JH7110_HDMITX0_MCLK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DOM_VOUT_TOP_CLK_HDMITX0_MCLK}};
//		case JH7110_HDMITX0_SCK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DOM_VOUT_TOP_CLK_HDMITX0_BCLK}};
		case JH7110_U0_PCLK_MUX_FUNC_PCLK: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_APB}};
		case JH7110_DISP_APB: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_PCLK_MUX_FUNC_PCLK}};
		case JH7110_U0_DC8200_CLK_PIX0_OUT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DC8200_CLK_PIX0}};
		case JH7110_U0_DC8200_CLK_PIX1_OUT: return {.fixFactor = {.mul = 1, .div = 1, .parent = JH7110_U0_DC8200_CLK_PIX1}};
		case JH7110_U0_DC8200_CLK_AXI: return {.composite = {.gate = 1, .parents = {JH7110_DISP_AXI}}};
		case JH7110_U0_DC8200_CLK_CORE: return {.composite = {.gate = 1, .parents = {JH7110_DISP_AXI}}};
		case JH7110_U0_DC8200_CLK_AHB: return {.composite = {.gate = 1, .parents = {JH7110_DISP_AHB}}};
		case JH7110_U0_MIPITX_DPHY_CLK_TXESC: return {.composite = {.gate = 1, .parents = {JH7110_TX_ESC}}};
		case JH7110_U0_CDNS_DSITX_CLK_SYS: return {.composite = {.gate = 1, .parents = {JH7110_DSI_SYS}}};
		case JH7110_U0_CDNS_DSITX_CLK_APB: return {.composite = {.gate = 1, .parents = {JH7110_DSI_SYS}}};
		case JH7110_U0_CDNS_DSITX_CLK_TXESC: return {.composite = {.gate = 1, .parents = {JH7110_TX_ESC}}};
		case JH7110_U0_HDMI_TX_CLK_SYS: return {.composite = {.gate = 1, .parents = {JH7110_DISP_APB}}};
		case JH7110_U0_HDMI_TX_CLK_MCLK: return {.composite = {.gate = 1, .parents = {JH7110_HDMITX0_MCLK}}};
		case JH7110_U0_HDMI_TX_CLK_BCLK: return {.composite = {.gate = 1, .parents = {JH7110_HDMITX0_SCK}}};
		default: return {.type = ClockDefType::empty};
	}
}


// #pragma mark - ClockDevice

DeviceNode*
Jh7110ClockDriver::ClockDevice::OwnerNode()
{
	fBase.fNode->AcquireReference();
	return fBase.fNode;
}


bool
Jh7110ClockDriver::ClockDevice::IsEnabled() const
{
	uint32 id = Id();
	ClockDef def = fBase.GetClockDef(id);

	switch (def.type) {
		case ClockDefType::composite:
			if (def.composite.gate)
				return fBase.GetRegs(id)->enable;
			break;
		default:
			break;
	}
	return true;
}


status_t
Jh7110ClockDriver::ClockDevice::SetEnabled(bool doEnable)
{
	uint32 id = Id();
	ClockDef def = fBase.GetClockDef(id);

	switch (def.type) {
		case ClockDefType::composite:
			if (def.composite.gate)
				fBase.GetRegs(id)->enable = doEnable;
			break;
		default:
			break;
	}
	return B_OK;
}


int64
Jh7110ClockDriver::ClockDevice::GetRate() const
{
	int32 id = Id();
	ClockDef def = fBase.GetClockDef(id);
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	auto GetDivider = [regs](::ClockDevice* parent, uint32 divWidth) -> int64 {
		int64 parentRate = parent->GetRate();
		if (parentRate < B_OK)
			return parentRate;

		uint32 div = regs->div & ((1 << divWidth) - 1);
		if (div == 0)
			return B_BAD_VALUE;

		return div_round_up(parentRate, div);
	};

	switch (def.type) {
		case ClockDefType::composite: {
			uint32 parentIndex = 0;
			if (def.composite.mux > 0)
				parentIndex = regs->mux & ((1 << def.composite.mux) - 1);

			::ClockDevice* parent = fBase.GetClock(def.composite.parents[parentIndex]);
			if (parent == NULL)
				return B_ERROR;

			if (def.composite.div > 0)
				return GetDivider(parent, def.composite.div);

			return parent->GetRate();
		}
		case ClockDefType::fixed:
			return def.fixed.rate;

		case ClockDefType::fixFactor: {
			::ClockDevice* parent = fBase.GetClock(def.fixFactor.parent);
			if (parent == NULL)
				return B_ERROR;

			return parent->GetRate() / def.fixFactor.div * def.fixFactor.mul;
		}
		default:
			return ENOSYS;
	}
}


int64
Jh7110ClockDriver::ClockDevice::SetRate(int64 rate)
{
	int32 id = Id();
	ClockDef def = fBase.GetClockDef(id);
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	if (rate <= 0)
		return B_BAD_VALUE;

	auto SetDivider = [regs, rate](::ClockDevice* parent, uint32 divWidth) -> int64 {
		int64 parentRate = parent->GetRate();
		uint32 div = div_round_up(parentRate, rate);
		div = std::min<uint32>(div, 1 << divWidth);
		regs->div = div;
		return div_round_up(parentRate, div);
	};

	switch (def.type) {
		case ClockDefType::composite: {
			uint32 parentIndex = 0;
			if (def.composite.mux > 0)
				parentIndex = regs->mux & ((1 << def.composite.mux) - 1);

			::ClockDevice* parent = fBase.GetClock(def.composite.parents[parentIndex]);
			if (parent == NULL)
				return B_ERROR;

			if (def.composite.div > 0)
				return SetDivider(parent, def.composite.div);

			break;
		}
		default:
			break;
	}
	return ENOSYS;
}


int64
Jh7110ClockDriver::ClockDevice::SetRateDry(int64 rate) const
{
	return ENOSYS;
}


ClockDevice*
Jh7110ClockDriver::ClockDevice::GetParent() const
{
	int32 id = Id();
	ClockDef def = fBase.GetClockDef(id);
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	switch (def.type) {
		case ClockDefType::composite: {
			uint32 parentIndex = 0;
			if (def.composite.mux > 0)
				parentIndex = regs->mux & ((1 << def.composite.mux) - 1);

			return fBase.GetClock(def.composite.parents[parentIndex]);
		}
		case ClockDefType::fixFactor:
			return fBase.GetClock(def.fixFactor.parent);

		default:
			break;
	}
	return NULL;
}


status_t
Jh7110ClockDriver::ClockDevice::SetParent(::ClockDevice* parent)
{
	int32 id = Id();
	ClockDef def = fBase.GetClockDef(id);
	volatile StarfiveClockRegs* regs = fBase.GetRegs(id);

	switch (def.type) {
		case ClockDefType::composite: {
			if (def.composite.mux <= 0)
				return ENOSYS;

			for (uint32 parentIndex = 0; parentIndex < (1U << def.composite.mux); parentIndex++) {
				if (parent == fBase.GetClock(def.composite.parents[parentIndex])) {
					regs->mux = parentIndex;
					return B_OK;
				}
			}
			return B_BAD_VALUE;
		}
		default:
			break;
	}
	return ENOSYS;
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
