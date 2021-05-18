#ifndef _HTIF_H_
#define _HTIF_H_

#include <stddef.h>
#include <stdint.h>


struct HtifRegs
{
	uint32_t toHostLo, toHostHi;
	uint32_t fromHostLo, fromHostHi;
};


extern HtifRegs *volatile gHtifRegs;


uint64_t HtifCmd(uint32_t device, uint8_t cmd, uint32_t arg);

void HtifShutdown();
void HtifOutChar(char ch);
void HtifOutString(const char *str);
void HtifOutString(const char *str, size_t len);


#endif	// _HTIF_H_
