/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "DrawingEngineBase.h"


#if DEBUG
#	define ASSERT_PARALLEL_LOCKED() \
	{ if (!IsParallelAccessLocked()) debugger("not parallel locked!"); }
#	define ASSERT_EXCLUSIVE_LOCKED() \
	{ if (!IsExclusiveAccessLocked()) debugger("not exclusive locked!"); }
#else
#	define ASSERT_PARALLEL_LOCKED()
#	define ASSERT_EXCLUSIVE_LOCKED()
#endif


DrawingEngineBase::DrawingEngineBase()
	:
	fGraphicsCard(NULL),
	fCopyToFront(true)
{
}


DrawingEngineBase::~DrawingEngineBase()
{
	SetHWInterface(NULL);
}


// #pragma mark - locking


bool
DrawingEngineBase::LockParallelAccess()
{
	return fGraphicsCard->LockParallelAccess();
}


#if DEBUG
bool
DrawingEngineBase::IsParallelAccessLocked() const
{
	return fGraphicsCard->IsParallelAccessLocked();
}
#endif


void
DrawingEngineBase::UnlockParallelAccess()
{
	fGraphicsCard->UnlockParallelAccess();
}


bool
DrawingEngineBase::LockExclusiveAccess()
{
	return fGraphicsCard->LockExclusiveAccess();
}


bool
DrawingEngineBase::IsExclusiveAccessLocked() const
{
	return fGraphicsCard->IsExclusiveAccessLocked();
}


void
DrawingEngineBase::UnlockExclusiveAccess()
{
	fGraphicsCard->UnlockExclusiveAccess();
}


// #pragma mark -


void
DrawingEngineBase::FrameBufferChanged()
{
}


void
DrawingEngineBase::SetHWInterface(HWInterface* interface)
{
	if (fGraphicsCard == interface)
		return;

	if (fGraphicsCard)
		fGraphicsCard->RemoveListener(this);

	fGraphicsCard = interface;

	if (fGraphicsCard)
		fGraphicsCard->AddListener(this);

	FrameBufferChanged();
}


void
DrawingEngineBase::SetCopyToFrontEnabled(bool enable)
{
	fCopyToFront = enable;
}


void
DrawingEngineBase::CopyToFront(/*const*/ BRegion& region)
{
	fGraphicsCard->InvalidateRegion(region);
}
