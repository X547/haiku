#ifndef _HTIF_H_
#define _HTIF_H_

#include <SupportDefs.h>

struct HtifRegs
{
	uint32 toHostLo, toHostHi;
	uint32 fromHostLo, fromHostHi;
};

extern HtifRegs *volatile gHtifRegs;

uint64 HtifCmd(uint32 device, uint8 cmd, uint32 arg);
void HtifShutdown();
void HtifOutChar(char ch);
void HtifOutString(const char *str);
void HtifOutString(const char *str, size_t len);

#endif	// _HTIF_H_
