/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef VIDEO_PRODUCER_HW_INTERACE_H
#define VIDEO_PRODUCER_HW_INTERACE_H


#include "HWInterface.h"

#include <Accelerant.h>
#include <Messenger.h>
#include <AutoDeleter.h>
#include <image.h>
#include <Region.h>
#include <String.h>
#include <AutoDeleterOS.h>
#include <ClientThreadLink.h>

#include <pthread.h>


class BBitmapBuffer;
class HWInterfaceProducer;
class CompositeProxy;


class VideoProducerHWInterface : public HWInterface {
public:
								VideoProducerHWInterface();
	virtual						~VideoProducerHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetFrameBufferConfig(
									frame_buffer_config& config);

	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float);
	virtual status_t			GetBrightness(float*);

	// cursor handling
	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
	virtual	void				MoveCursorTo(float x, float y);

	// overlay support
	virtual overlay_token		AcquireOverlayChannel();
	virtual void				ReleaseOverlayChannel(overlay_token token);

	virtual status_t			GetOverlayRestrictions(const Overlay* overlay,
									overlay_restrictions* restrictions);
	virtual bool				CheckOverlayRestrictions(int32 width,
									int32 height, color_space colorSpace);
	virtual const overlay_buffer* AllocateOverlayBuffer(int32 width,
									int32 height, color_space space);
	virtual void				FreeOverlayBuffer(const overlay_buffer* buffer);

	virtual void				ConfigureOverlay(Overlay* overlay);
	virtual void				HideOverlay(Overlay* overlay);

	// frame buffer access
	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			InvalidateRegion(const BRegion& region);
	virtual	status_t			Invalidate(const BRect& frame);

protected:
	virtual	void				_DrawCursor(IntRect area) const;

private:
	friend class HWInterfaceProducer;

	class Transaction: public BReferenceable {
	private:
		VideoProducerHWInterface &fBase;
		BRegion fRegion;
		bool fCompleted = false;

		pthread_mutex_t fMutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_cond_t fCond = PTHREAD_COND_INITIALIZER;

	public:
		Transaction(VideoProducerHWInterface &base): fBase(base) {}
		void Add(const BRegion &dirty) {fRegion.Include(&dirty);}
		void Commit();
		void WaitForCompletion();
		void Complete();
	};

	class Queue {
	private:
		VideoProducerHWInterface &fBase;
		pthread_mutex_t fMutex = PTHREAD_MUTEX_INITIALIZER;

		enum {fMaxLen = 2};
		BReference<Transaction> fItems[fMaxLen];
		uint32 fBeg = 0, fLen = 0;

	public:
		Queue(VideoProducerHWInterface &base): fBase(base) {}
		pthread_mutex_t *GetMutex() {return &fMutex;}
		uint32 Length() {return fLen;}
		BReference<Transaction> Insert();
		BReference<Transaction> Remove();
		BReference<Transaction> First();
		BReference<Transaction> Last();
	} fQueue;

	BMessenger					fRadeonGfxMsgr;
	ClientThreadLinkConnection	fRadeonGfxConn;
	BMessenger					fCompositeProducerMsgr;
	ObjectDeleter<CompositeProxy>
								fCompositor;
	BMessenger					fBaseSurface;

	ObjectDeleter<HWInterfaceProducer>
								fProducer;

	ObjectDeleter<RenderingBuffer>
								fBackBuffer,
								fFrontBuffer;
};

#endif // VIDEO_PRODUCER_HW_INTERACE_H
