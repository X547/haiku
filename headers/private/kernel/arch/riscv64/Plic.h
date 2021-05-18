#ifndef _PLIC_H_
#define _PLIC_H_

#include <stdint.h>


struct PlicRegs
{
	// context = hart*2 + mode, mode: 0 - machine, 1 - supervisor
	uint32_t priority[1024];          // 0x000000
	uint32_t pending[1024/32];        // 0x001000, bitfield
	uint8_t unused1[0x2000 - 0x1080]; // 
	uint32_t enable[15872][1024/32];  // 0x002000, bitfield, [context][enable]
	uint8_t unused2[0xe000];          // 
	struct {
		uint32_t priorityThreshold;
		uint32_t claimAndComplete;
		uint8_t unused[0xff8];
	} contexts[15872];                // 0x200000
};

extern PlicRegs *volatile gPlicRegs;


#endif	// _PLIC_H_
