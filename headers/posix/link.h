#ifndef _LINK_H_
#define _LINK_H_

#include <os/kernel/elf.h>


struct dl_phdr_info
{
	Elf_Addr dlpi_addr;			/* module relocation base */
	const char *dlpi_name;			/* module name */
	const Elf_Phdr *dlpi_phdr;		/* pointer to module's phdr */
	Elf_Half dlpi_phnum;			/* number of entries in phdr */
	unsigned long long int dlpi_adds;	/* total # of loads */
	unsigned long long int dlpi_subs;	/* total # of unloads */
	size_t dlpi_tls_modid;
	void *dlpi_tls_data;
};


typedef int (*__dl_iterate_hdr_callback)(struct dl_phdr_info *, size_t, void *);
extern int dl_iterate_phdr(__dl_iterate_hdr_callback, void *);


#endif	// _LINK_H_
