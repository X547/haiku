#ifndef _ARCH_TRAPS_H_
#define _ARCH_TRAPS_H_


#include <SupportDefs.h>


struct iframe;


extern "C" {

void SVec();
void STrap(iframe* frame);

};


void arch_traps_init();


#endif	// _ARCH_TRAPS_H_
