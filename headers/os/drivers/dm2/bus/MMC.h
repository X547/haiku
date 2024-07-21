#pragma once

#include <dm2/device_manager.h>


// Device attribute paths for the MMC device
#define MMC_DEVICE_RCA	"mmc/rca"	/* uint16 */
#define MMC_DEVICE_TYPE	"mmc/type"	/* uint8 */


struct generic_io_vec;
class IOOperation;


enum {
	CARD_TYPE_MMC,
	CARD_TYPE_SD,
	CARD_TYPE_SDHC,
	CARD_TYPE_UHS1,
	CARD_TYPE_UHS2,
	CARD_TYPE_SDIO
};


// Commands for SD cards defined in SD Specifications Part 1:
// Physical Layer Simplified Specification Version 8.00
// They are in the common .h file for the mmc stack because the SDHCI driver
// currently needs to map them to the corresponding expected response types.
enum SD_COMMANDS {
	// Basic commands, class 0
	SD_GO_IDLE_STATE 		= 0,
	SD_ALL_SEND_CID			= 2,
	SD_SEND_RELATIVE_ADDR	= 3,
	SD_SELECT_DESELECT_CARD	= 7,
	SD_SEND_IF_COND			= 8,
	SD_SEND_CSD				= 9,
	SD_STOP_TRANSMISSION	= 12,
	SD_SEND_STATUS			= 13,
	SD_SET_BLOCKLEN			= 16,

	// Block oriented read and write commands, class 2
	SD_READ_SINGLE_BLOCK = 17,
	SD_READ_MULTIPLE_BLOCKS = 18,

	SD_WRITE_SINGLE_BLOCK = 24,
	SD_WRITE_MULTIPLE_BLOCKS = 25,

	// Erase commands, class 5
	SD_ERASE_WR_BLK_START = 32,
	SD_ERASE_WR_BLK_END = 33,
	SD_ERASE = 38,

	// Application specific commands, class 8
	SD_APP_CMD = 55,

	// I/O mode commands, class 9
	SD_IO_ABORT = 52,
};


enum SDHCI_APPLICATION_COMMANDS {
	SD_SET_BUS_WIDTH = 6,
	SD_SEND_OP_COND = 41,
	SD_SEND_SCR = 51,
};


struct mmc_command {
	uint8 command;
	uint32 argument;
	bool isWideResponse: 1;
	bool doCheckCrc: 1;
	uint32* response;
};

struct mmc_data {
	bool isWrite;
	uint32 blockSize;
	uint32 blockCnt;
	uint32 vecCount;
	generic_io_vec* vecs;
};


class MmcBus {
public:
	static inline const char ifaceName[] = "busses/mmc";

	virtual status_t SetClock(uint32 kilohertz) = 0;
		// Configure the bus clock. The bus is initialized with a slow clock
		// that allows device enumeration in all cases, but after enumeration
		// the mmc_bus knows how fast each card can go, and configures the bus
		// accordingly.
	virtual status_t ExecuteCommand(uint8 command, uint32 argument, uint32* result) = 0;
		// Execute a command with no I/O phase
	virtual status_t SetBusWidth(int width) = 0;
		// Set the data bus width to 1, 4 or 8 bit mode.

	virtual status_t ExecuteCommand(const mmc_command& cmd, const mmc_data* data) = 0;

protected:
	~MmcBus() = default;
};


class MmcDevice {
public:
	static inline const char ifaceName[] = "bus_managers/mmc/device";

	virtual MmcBus* GetBus() = 0;

protected:
	~MmcDevice() = default;
};
