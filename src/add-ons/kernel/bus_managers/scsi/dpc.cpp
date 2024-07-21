/*
** Copyright 2002/03, Thomas Kurschel. All rights reserved.
** Distributed under the terms of the MIT License.
*/

/*
	Part of Open SCSI bus manager

	DPC handling (deferred procedure calls).

	DPC are executed by the service thread of the bus
	(see busses.c).
*/

#include "scsi_internal.h"

#include <string.h>
#include <stdlib.h>
#include <new>


status_t
ScsiBusImpl::AllocDpc(ScsiBusDpc **dpc)
{
	SHOW_FLOW0(3, "");

	*dpc = new(std::nothrow) ScsiDpcImpl();
	if (*dpc == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


void
ScsiDpcImpl::Free()
{
	SHOW_FLOW0(3, "");

	delete this;
}


status_t
ScsiBusImpl::ScheduleDpc(ScsiBusDpc *inDpc, /*int flags,*/
	void (*func)(void *arg), void *arg)
{
	ScsiBusImpl *bus = this;
	ScsiDpcImpl *dpc = static_cast<ScsiDpcImpl*>(inDpc);
	SHOW_FLOW(3, "bus=%p, dpc=%p", bus, dpc);
	acquire_spinlock_irq(&bus->dpc_lock);

	dpc->func = func;
	dpc->arg = arg;

	if (!dpc->registered) {
		dpc->registered = true;
		dpc->next = bus->dpc_list;
		bus->dpc_list = dpc;
	} else
		SHOW_FLOW0(3, "already registered - ignored");

	release_spinlock_irq(&bus->dpc_lock);

	// this is called in IRQ context, so scheduler is not allowed
	release_sem_etc(bus->start_service, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


/** execute pending DPCs */

bool
scsi_check_exec_dpc(ScsiBusImpl *bus)
{
	SHOW_FLOW(3, "bus=%p, dpc_list=%p", bus, bus->dpc_list);
	acquire_spinlock_irq(&bus->dpc_lock);

	if (bus->dpc_list) {
		ScsiDpcImpl *dpc;
		void (*dpc_func)(void *);
		void *dpc_arg;

		dpc = bus->dpc_list;
		bus->dpc_list = dpc->next;

		dpc_func = dpc->func;
		dpc_arg = dpc->arg;
		dpc->registered = false;

		release_spinlock_irq(&bus->dpc_lock);

		dpc_func(dpc_arg);
		return true;
	}

	release_spinlock_irq(&bus->dpc_lock);
	return false;
}

