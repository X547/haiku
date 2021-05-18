#ifndef _CLINT_H_
#define _CLINT_H_

#include <stdint.h>


enum {
	clintSecond = 10000000 // unit of ClintRegs::mTime
};

// core local interruptor
struct ClintRegs
{
	uint8_t unknown1[0x4000];
	uint64_t mTimeCmp[4095]; // per CPU core, but not implemented in temu
	uint64_t mTime;          // @0xBFF8
};

extern ClintRegs *volatile gClintRegs;


#endif	// _CLINT_H_
