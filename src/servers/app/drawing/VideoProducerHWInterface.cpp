#include "VideoProducerHWInterface.h"

#include <VideoProducer.h>
#include <CompositeProducer.h>
#include <CompositeProxy.h>
#include <VideoBuffer.h>
#include <VideoBufferBindSW.h>

#include <Application.h>
#include <Bitmap.h>
#include <Window.h>
#include <View.h>
#include <MessageFilter.h>
#include <Cursor.h>

#include <new>
#include <stdio.h>

#include <InterfacePrivate.h>
#include <ServerProtocol.h>
#include <PthreadMutexLocker.h>
#include "PortLink.h"
#include "BBitmapBuffer.h"
#include "AppKitPtrs.h"

enum {
	// DRM
	radeonMmapMsg = userMsgBase,
	radeonIoctlMsg,

	radeonListTeams,
	radeonListBuffers,
	radeonGetMemoryUsage,

	radeonThermalQuery,
	radeonSetClocks,
	// display
	radeonGetDisplayConsumer,
	radeonUpdateCursor
};

enum {
	cursorUpdateEnabled,
	cursorUpdatePos,
	cursorUpdateOrg,
	cursorUpdateBuffer,
	cursorUpdateFormat,
};


//#pragma mark -

const unsigned char kEmptyCursor[] = { 16, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

class InputView : public BView {
public:
								InputView(BRect bounds);
	virtual						~InputView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

								// InputView
			void				ForwardMessage(BMessage* message = NULL);

private:
			port_id				fInputPort;
};

class InputWindow : public BWindow {
public:
								InputWindow(BRect frame);
	virtual						~InputWindow();

	virtual	bool				QuitRequested();

private:
			InputView*			fView;
};

class InputMessageFilter : public BMessageFilter {
public:
								InputMessageFilter(InputView* view);

	virtual filter_result		Filter(BMessage* message, BHandler** _target);

private:
			InputView*			fView;
};




InputView::InputView(BRect bounds)
	:
	BView(bounds, "graphics card view", B_FOLLOW_ALL, B_WILL_DRAW)
{
#ifndef INPUTSERVER_TEST_MODE
	fInputPort = create_port(200, SERVER_INPUT_PORT);
#else
	fInputPort = create_port(100, "ViewInputDevice");
#endif

	AddFilter(new InputMessageFilter(this));
}


InputView::~InputView()
{
}


void
InputView::AttachedToWindow()
{
}


void
InputView::Draw(BRect updateRect)
{
}


/*!	These functions emulate the Input Server by sending the *exact* same kind of
	messages to the server's port. Being we're using a regular window, it would
	make little sense to do anything else.
*/
void
InputView::ForwardMessage(BMessage* message)
{
	if (message == NULL)
		message = Window()->CurrentMessage();
	if (message == NULL)
		return;

	// remove some fields that potentially mess up our own message processing
	BMessage copy = *message;
	copy.RemoveName("screen_where");
	copy.RemoveName("be:transit");
	copy.RemoveName("be:view_where");
	copy.RemoveName("be:cursor_needed");
	copy.RemoveName("_view_token");

	size_t length = copy.FlattenedSize();
	char stream[length];

	if (copy.Flatten(stream, length) == B_OK)
		write_port(fInputPort, 0, stream, length);
}


void
InputView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		default:
			BView::MessageReceived(message);
			break;
	}
}


InputMessageFilter::InputMessageFilter(InputView* view)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fView(view)
{
}


filter_result
InputMessageFilter::Filter(BMessage* message, BHandler** target)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_UNMAPPED_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_UP:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_WHEEL_CHANGED:
			if (message->what == B_MOUSE_DOWN)
				fView->SetMouseEventMask(B_POINTER_EVENTS);

			fView->ForwardMessage(message);
			return B_SKIP_MESSAGE;

		case B_MOUSE_MOVED:
		{
			int32 transit;
			if (message->FindInt32("be:transit", &transit) == B_OK
				&& transit == B_ENTERED_VIEW) {
				// A bug in R5 prevents this call from having an effect if
				// called elsewhere, and calling it here works, if we're lucky :-)
				BCursor cursor(kEmptyCursor);
				fView->SetViewCursor(&cursor, true);
			}
			fView->ForwardMessage(message);
			return B_SKIP_MESSAGE;
		}
	}

	return B_DISPATCH_MESSAGE;
}


InputWindow::InputWindow(BRect frame)
	:
	BWindow(frame, "Haiku App Server", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_NO_SERVER_SIDE_WINDOW_MODIFIERS)
{
	fView = new InputView(Bounds());
	AddChild(fView);
	fView->MakeFocus();
		// make it receive key events
}


InputWindow::~InputWindow()
{
}


bool
InputWindow::QuitRequested()
{
	port_id serverport = find_port(SERVER_PORT_NAME);

	if (serverport >= 0) {
		BPrivate::PortLink link(serverport);
		link.StartMessage(B_QUIT_REQUESTED);
		link.Flush();
	} else
		printf("ERROR: couldn't find the app_server's main port!");

	// we don't quit on ourself, we let us be Quit()!
	return false;
}


//#pragma mark -

static int32
run_app_thread(void* cookie)
{
	if (BApplication* app = (BApplication*)cookie) {
		app->Lock();
		app->Run();
		delete app;
	}
	return 0;
}


static status_t
check_app_running()
{
	if (be_app == NULL) {
		status_t ret;
		BApplication* app = new BApplication(
			"application/x-vnd.Haiku-test-app_server");
		app->Unlock();

		(new InputWindow(BRect(0, 0, 1024 - 1, 768 - 1).OffsetByCopy(32, 32)))->Show();

		thread_id appThread = spawn_thread(run_app_thread, "app thread",
			B_NORMAL_PRIORITY, app);
		if (appThread >= B_OK)
			ret = resume_thread(appThread);
		else
			ret = appThread;

		if (ret < B_OK)
			return ret;
	}
	return B_OK;
}


//#pragma mark -

static bool FindConsumerGfx(BMessenger& consumer)
{
	BMessenger consumerApp("application/x-vnd.X512-RadeonGfx");
	if (!consumerApp.IsValid()) {
		printf("[!] No TestConsumer\n");
		return false;
	}
	for (int32 i = 0; ; i++) {
		BMessage reply;
		{
			BMessage scriptMsg(B_GET_PROPERTY);
			scriptMsg.AddSpecifier("Handler", i);
			consumerApp.SendMessage(&scriptMsg, &reply);
		}
		int32 error;
		if (reply.FindInt32("error", &error) >= B_OK && error < B_OK)
			return false;
		if (reply.FindMessenger("result", &consumer) >= B_OK) {
			BMessage scriptMsg(B_GET_PROPERTY);
			scriptMsg.AddSpecifier("InternalName");
			consumer.SendMessage(&scriptMsg, &reply);
			const char* name;
			if (reply.FindString("result", &name) >= B_OK && strcmp(name, "RadeonGfxConsumer") == 0)
				return true;
		}
	}
}


class VideoStreamsRenBuf: public RenderingBuffer {
 public:
								VideoStreamsRenBuf(const VideoBuffer &buf, void *bits);
	virtual						~VideoStreamsRenBuf();

	virtual	status_t			InitCheck() const;
	virtual	bool				IsGraphicsMemory() const { return false; }

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

 private:
			VideoBuffer fBuf;
			void *fBits;
};

VideoStreamsRenBuf::VideoStreamsRenBuf(const VideoBuffer &buf, void *bits):
	fBuf(buf), fBits(bits)
{}

VideoStreamsRenBuf::~VideoStreamsRenBuf()
{}

status_t VideoStreamsRenBuf::InitCheck() const
{
	if (fBits == NULL) return B_NO_INIT;
	return B_OK;
}

color_space VideoStreamsRenBuf::ColorSpace() const {return fBuf.format.colorSpace;}
void* VideoStreamsRenBuf::Bits() const {return fBits;}
uint32 VideoStreamsRenBuf::BytesPerRow() const {return fBuf.format.bytesPerRow;}
uint32 VideoStreamsRenBuf::Width() const {return fBuf.format.width;}
uint32 VideoStreamsRenBuf::Height() const {return fBuf.format.height;}


class HWInterfaceProducer final: public VideoProducer
{
private:
	friend class VideoProducerHWInterface;
	VideoProducerHWInterface *fBase;

	SwapChainBindSW fSwapChainBind;

	uint32 fValidPrevBufCnt;
	BRegion fPrevDirty;
	BRegion fPendingDirty;

public:
	HWInterfaceProducer(VideoProducerHWInterface* base, const char* name);
	virtual ~HWInterfaceProducer();

	void Connected(bool isActive) final;
	void SwapChainChanged(bool isValid) final;
	void Presented(const PresentedInfo &presentedInfo) final;
};


HWInterfaceProducer::HWInterfaceProducer(VideoProducerHWInterface* base, const char* name)
	:
	VideoProducer(name),
	fBase(base)
{
}


HWInterfaceProducer::~HWInterfaceProducer()
{
}


void
HWInterfaceProducer::Connected(bool isActive)
{
	if (isActive) {
		SwapChainSpec spec{};
		spec.size = sizeof(SwapChainSpec);
		spec.presentEffect = presentEffectCopy;
		spec.bufferCnt = 2;
		spec.kind = bufferRefArea;
		spec.colorSpace = B_RGBA32;
		if (RequestSwapChain(spec) < B_OK) {
			printf("[!] can't request swap chain\n");
			exit(1);
		}
		fValidPrevBufCnt = 0;
		fPrevDirty.MakeEmpty();
	}
}


void
HWInterfaceProducer::SwapChainChanged(bool isValid)
{
	printf("HWInterfaceProducer::SwapChainChanged(%d)\n", isValid);
	VideoProducer::SwapChainChanged(isValid);
	fSwapChainBind.Unset();
	if (!isValid) {
		return;
	}
	fSwapChainBind.ConnectTo(GetSwapChain());
}


void
HWInterfaceProducer::Presented(const PresentedInfo &presentedInfo)
{
	//printf("HWInterfaceProducer::Presented()\n");
	PthreadMutexLocker lock(fBase->fQueue.GetMutex());
	BReference<VideoProducerHWInterface::Transaction> tr = fBase->fQueue.Remove();
	tr->Complete();
	if (fBase->fQueue.Length() > 0) {
		fBase->fQueue.First()->Commit();
	}
}


//#pragma mark -


void VideoProducerHWInterface::Transaction::Commit()
{
	AppKitPtrs::ExternalPtr(fBase.fProducer.Get()).Lock()->Present(&fRegion);
}

void VideoProducerHWInterface::Transaction::WaitForCompletion()
{
	pthread_mutex_lock(&fMutex);
	while (!fCompleted) {
		pthread_cond_wait(&fCond, &fMutex);
	}
	pthread_mutex_unlock(&fMutex);
}

void VideoProducerHWInterface::Transaction::Complete()
{
	pthread_mutex_lock(&fMutex);
	fCompleted = true;
	pthread_cond_broadcast(&fCond);
	pthread_mutex_unlock(&fMutex);
}


BReference<VideoProducerHWInterface::Transaction> VideoProducerHWInterface::Queue::Insert()
{
	BReference<Transaction> res(new Transaction(fBase), true);
	fItems[(fBeg + fLen) % fMaxLen] = res;
	fLen++;
	return res;
}

BReference<VideoProducerHWInterface::Transaction> VideoProducerHWInterface::Queue::Remove()
{
	BReference<Transaction> res = fItems[fBeg];
	fItems[fBeg].Unset();
	fBeg = (fBeg + 1) % fMaxLen;
	fLen--;
	return res;
}

BReference<VideoProducerHWInterface::Transaction> VideoProducerHWInterface::Queue::First()
{
	return fItems[fBeg];
}

BReference<VideoProducerHWInterface::Transaction> VideoProducerHWInterface::Queue::Last()
{
	return fItems[(fBeg + fLen - 1) % fMaxLen];
}


//#pragma mark -

static bool FindCompositor(BMessenger& compositor)
{
	BMessenger consumerApp("application/x-vnd.VideoStreams-Compositor");
	if (!consumerApp.IsValid()) {
		printf("[!] No TestConsumer\n");
		return false;
	}
	for (int32 i = 0; ; i++) {
		BMessage reply;
		{
			BMessage scriptMsg(B_GET_PROPERTY);
			scriptMsg.AddSpecifier("Handler", i);
			consumerApp.SendMessage(&scriptMsg, &reply);
		}
		int32 error;
		if (reply.FindInt32("error", &error) >= B_OK && error < B_OK)
			return false;
		if (reply.FindMessenger("result", &compositor) >= B_OK) {
			BMessage scriptMsg(B_GET_PROPERTY);
			scriptMsg.AddSpecifier("InternalName");
			compositor.SendMessage(&scriptMsg, &reply);
			const char* name;
			if (reply.FindString("result", &name) >= B_OK && strcmp(name, "compositeProducer") == 0)
				return true;
		}
	}
}

inline void Check(status_t res) {if (res < B_OK) {fprintf(stderr, "Error: %s\n", strerror(res)); abort();}}

VideoProducerHWInterface::VideoProducerHWInterface()
	:
	fQueue(*this)
{
	printf("+VideoProducerHWInterface\n");
	check_app_running();

	fRadeonGfxMsgr.SetTo("application/x-vnd.X512-RadeonGfx");
	if (!fRadeonGfxMsgr.IsValid()) {
		printf("[!] RadeonGfx is not running\n");
		exit(1);
	}
	fRadeonGfxConn.SetMessenger(fRadeonGfxMsgr);

	BMessenger fCompositeProducerMsgr;
	if (!FindCompositor(fCompositeProducerMsgr)) {
		exit(1);
	}
	fCompositor.SetTo(new CompositeProxy(fCompositeProducerMsgr));

	fProducer.SetTo(new HWInterfaceProducer(this, "hwInterfaceProducer"));
	AppKitPtrs::LockedPtr(be_app)->AddHandler(fProducer.Get());

	SurfaceUpdate surfaceInfo = {
		.valid = (1 << surfaceFrame) | (1 << surfaceDrawMode),
		.frame = BRect(0, 0, 1920 - 1, 1080 - 1),
		.drawMode = B_OP_COPY
	};

	Check(fCompositor->NewSurface(fBaseSurface, "app_server", surfaceInfo));
	Check(fProducer->ConnectTo(fBaseSurface));
#if 0
	ThreadLinkHolder link(fRadeonGfxConn);

	int32 crtc = 0;
	int32 reply = 0;
	BMessenger consumer;
	link.StartMessage(radeonGetDisplayConsumer);
	link.Attach(crtc);
	link.FlushWithReply(reply); if (reply < B_OK) {printf("[!] bad reply\n"); exit(1);}
	link.Read(&consumer);

	printf("consumer: "); WriteMessenger(consumer); printf("\n");
	be_app->Lock();
	fProducer.SetTo(new HWInterfaceProducer(this, "hwInterfaceProducer"));
	be_app->AddHandler(fProducer.Get());
	printf("+VideoFileProducer: "); WriteMessenger(BMessenger(fProducer.Get())); printf("\n");

	if (fProducer->ConnectTo(consumer) < B_OK) {
		printf("[!] can't connect to consumer\n");
		exit(1);
	}

	be_app->Unlock();
#endif

	fHardwareCursorEnabled = true;
}


VideoProducerHWInterface::~VideoProducerHWInterface()
{
	printf("-VideoProducerHWInterface\n");
	fProducer->ConnectTo(BMessenger());
	Check(fCompositor->DeleteSurface(fBaseSurface));
}


status_t
VideoProducerHWInterface::Initialize()
{
	printf("VideoProducerHWInterface::Initialize()\n");
	return B_OK;
}


status_t
VideoProducerHWInterface::Shutdown()
{
	printf("VideoProducerHWInterface::Shutdown()\n");
	return B_OK;
}


status_t
VideoProducerHWInterface::SetMode(const display_mode& mode)
{
	AutoWriteLocker _(this);
	printf("VideoProducerHWInterface::SetMode()\n");
	printf("  fProducer: %p\n", fProducer.Get());
	printf("  fProducer->fSwapChainBind.Buffers(): %p\n", fProducer->fSwapChainBind.Buffers());
	printf("  fProducer->fSwapChainBind.Buffers()[fProducer->RenderBufferId()].bits: %p\n", fProducer->fSwapChainBind.Buffers()[fProducer->RenderBufferId()].bits);

	BRect frame(0, 0, mode.virtual_width - 1, mode.virtual_height - 1);
	fBackBuffer.SetTo(new VideoStreamsRenBuf(*fProducer->RenderBuffer(), fProducer->fSwapChainBind.Buffers()[fProducer->RenderBufferId()].bits));
	fFrontBuffer.SetTo(new VideoStreamsRenBuf(*fProducer->RenderBuffer(), fProducer->fSwapChainBind.Buffers()[fProducer->RenderBufferId()].bits));

	_NotifyFrameBufferChanged();

	return B_OK;
}


void
VideoProducerHWInterface::GetMode(display_mode* mode)
{
	AutoReadLocker _(this);
	//printf("VideoProducerHWInterface::GetMode()\n");
	uint16 width = 1920;
	uint16 height = 1080;
	*mode = display_mode{
		.timing {
			.h_display = width,
			.v_display = height,
		},
		.space = B_RGBA32,
		.virtual_width = width,
		.virtual_height = height
	};
}


status_t
VideoProducerHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	//printf("VideoProducerHWInterface::GetDeviceInfo()\n");
	AutoReadLocker _(this);
	info->version = 100;
	sprintf(info->name, "VideoProducerHWInterface");
	sprintf(info->chipset, "RadeonGfx");
	sprintf(info->serial_no, "unknown");
	info->memory = 2*1024*1024*1024;
	info->dac_speed = 0xFFFFFFFF;
	return B_OK;
}


status_t
VideoProducerHWInterface::GetFrameBufferConfig(frame_buffer_config& config)
{
	printf("VideoProducerHWInterface::GetFrameBufferConfig()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	AutoReadLocker _(this);
	printf("VideoProducerHWInterface::GetModeList()\n");

	display_mode *modes = new(std::nothrow) display_mode[1];
	GetMode(&modes[0]);

	*_modeList = modes;
	*_count = 1;

	return B_OK;
}


status_t
VideoProducerHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	printf("VideoProducerHWInterface::GetPixelClockLimits()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	printf("VideoProducerHWInterface::GetTimingConstraints()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::ProposeMode(display_mode* candidate, const display_mode* low, const display_mode* high)
{
	printf("VideoProducerHWInterface::ProposeMode()\n");
	return B_ERROR;
}


sem_id
VideoProducerHWInterface::RetraceSemaphore()
{
	printf("VideoProducerHWInterface::RetraceSemaphore()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::WaitForRetrace(bigtime_t timeout)
{
	printf("VideoProducerHWInterface::WaitForRetrace()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::SetDPMSMode(uint32 state)
{
	printf("VideoProducerHWInterface::SetDPMSMode()\n");
	return B_ERROR;
}


uint32
VideoProducerHWInterface::DPMSMode()
{
	printf("VideoProducerHWInterface::DPMSMode()\n");
	return B_DPMS_ON;
}


uint32
VideoProducerHWInterface::DPMSCapabilities()
{
	printf("VideoProducerHWInterface::DPMSCapabilities()\n");
	return 0;
}


status_t
VideoProducerHWInterface::SetBrightness(float val)
{
	printf("VideoProducerHWInterface::SetBrightness()\n");
	return B_ERROR;
}


status_t
VideoProducerHWInterface::GetBrightness(float* val)
{
	printf("VideoProducerHWInterface::GetBrightness()\n");
	return B_ERROR;
}


void
VideoProducerHWInterface::SetCursor(ServerCursor* cursor)
{
	//printf("VideoProducerHWInterface::SetCursor()\n");
	ThreadLinkHolder link(fRadeonGfxConn);

	int32 crtc = 0;
	int32 reply = 0;
	link.StartMessage(radeonUpdateCursor);
	link.Attach(crtc);
	link.Attach<uint32>(
		(1 << cursorUpdateOrg) |
		(1 << cursorUpdateBuffer) |
		(1 << cursorUpdateFormat)
	);
	uint32 width = (uint32)cursor->Bounds().Width() + 1;
	uint32 height = (uint32)cursor->Bounds().Height() + 1;
	link.Attach((int32)cursor->GetHotSpot().x);
	link.Attach((int32)cursor->GetHotSpot().y);
	link.Attach((int32)cursor->BytesPerRow());
	link.Attach(width);
	link.Attach(height);
	link.Attach((int32)cursor->ColorSpace());
	link.Attach(cursor->Bits(), cursor->BytesPerRow()*height);
	link.FlushWithReply(reply); if (reply < B_OK) {printf("[!] bad reply\n");}

	HWInterface::SetCursor(cursor);
}


void
VideoProducerHWInterface::SetCursorVisible(bool visible)
{
	//printf("VideoProducerHWInterface::SetCursorVisible()\n");
	HWInterface::SetCursorVisible(visible);

	ThreadLinkHolder link(fRadeonGfxConn);

	int32 crtc = 0;
	int32 reply = 0;
	link.StartMessage(radeonUpdateCursor);
	link.Attach(crtc);
	link.Attach<uint32>(1 << cursorUpdateEnabled);
	link.Attach(visible);
	link.FlushWithReply(reply); if (reply < B_OK) {printf("[!] bad reply\n");}
}


void
VideoProducerHWInterface::MoveCursorTo(float x, float y)
{
	//printf("VideoProducerHWInterface::MoveCursorTo()\n");
	HWInterface::MoveCursorTo(x, y);

	ThreadLinkHolder link(fRadeonGfxConn);

	int32 crtc = 0;
	int32 reply = 0;
	link.StartMessage(radeonUpdateCursor);
	link.Attach(crtc);
	link.Attach<uint32>(1 << cursorUpdatePos);
	link.Attach((int32)x);
	link.Attach((int32)y);
	link.FlushWithReply(reply); if (reply < B_OK) {printf("[!] bad reply\n");}
}


void
VideoProducerHWInterface::_DrawCursor(IntRect area) const
{
	printf("VideoProducerHWInterface::_DrawCursor()\n");
	//HWInterface::_DrawCursor(area);
}


// #pragma mark - overlays

overlay_token
VideoProducerHWInterface::AcquireOverlayChannel()
{
	overlay_token token = malloc(sizeof(void*));
	printf("AcquireOverlayChannel() -> %p\n", token);
	return token;
}


void
VideoProducerHWInterface::ReleaseOverlayChannel(overlay_token token)
{
	printf("ReleaseOverlayChannel(%p)\n", token);
	free(token);
}


status_t
VideoProducerHWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	printf("GetOverlayRestrictions(%p)\n", overlay);
	if (overlay == NULL || restrictions == NULL)
		return B_BAD_VALUE;

	memset(restrictions, 0, sizeof(overlay_restrictions));
	restrictions->min_width_scale = 0.25;
	restrictions->max_width_scale = 8.0;
	restrictions->min_height_scale = 0.25;
	restrictions->max_height_scale = 8.0;

	return B_OK;
}


bool
VideoProducerHWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space colorSpace)
{
	printf("CheckOverlayRestrictions()\n");
	return true;
}


const overlay_buffer*
VideoProducerHWInterface::AllocateOverlayBuffer(int32 width, int32 height, color_space space)
{
	printf("AllocateOverlayBuffer(%" B_PRId32 ", %" B_PRId32 ", %u)\n", width, height, space);
	ObjectDeleter<overlay_buffer> buf = new(std::nothrow) overlay_buffer;
	if (!buf.IsSet())
		return NULL;

	buf->space = space;
	buf->width = width;
	buf->height = height;
	buf->bytes_per_row = BPrivate::get_bytes_per_row(space, width);
	ArrayDeleter<uint8> buffer(new(std::nothrow) uint8[buf->bytes_per_row*buf->height]);
	if (!buffer.IsSet())
		return NULL;

	buf->buffer = buffer.Detach();
	buf->buffer_dma = NULL;

	return buf.Detach();
}


void
VideoProducerHWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
	printf("FreeOverlayBuffer(%p)\n", buffer);
	ObjectDeleter<const overlay_buffer> buf(buffer);
	ArrayDeleter<uint8> bufferData((uint8*)buf->buffer);
}


void
VideoProducerHWInterface::ConfigureOverlay(Overlay* overlay)
{
	printf("VideoProducerHWInterface::ConfigureOverlay(%p)\n", overlay);
#if 0
	fAccConfigureOverlay(overlay->OverlayToken(), overlay->OverlayBuffer(),
		overlay->OverlayWindow(), overlay->OverlayView());
#endif
}


void
VideoProducerHWInterface::HideOverlay(Overlay* overlay)
{
	printf("VideoProducerHWInterface::HideOverlay(%p)\n", overlay);
#if 0
	fAccConfigureOverlay(overlay->OverlayToken(), overlay->OverlayBuffer(),
		NULL, NULL);
#endif
}


//#pragma mark -

RenderingBuffer*
VideoProducerHWInterface::FrontBuffer() const
{
	// do not allow access to front buffer
	return NULL;
}


RenderingBuffer*
VideoProducerHWInterface::BackBuffer() const
{
	return fBackBuffer.Get();
}


bool
VideoProducerHWInterface::IsDoubleBuffered() const
{
	return true;
}


status_t
VideoProducerHWInterface::InvalidateRegion(const BRegion& dirty)
{
	//printf("VideoProducerHWInterface::InvalidateRegion()\n");
	if (dirty.CountRects() == 0) return B_OK;

	PthreadMutexLocker lock(fQueue.GetMutex());
	if (fQueue.Length() == 0) {
		BReference<Transaction> tr = fQueue.Insert();
		tr->Add(dirty);
		tr->Commit();
		lock.Unlock();
		tr->WaitForCompletion();
		return B_OK;
	}
	if (fQueue.Length() < 2) {
		fQueue.Insert();
	}
	BReference<Transaction> tr = fQueue.Last();
	tr->Add(dirty);
	lock.Unlock();
	tr->WaitForCompletion();
	return B_OK;
}


status_t
VideoProducerHWInterface::Invalidate(const BRect& frame)
{
	//printf("VideoProducerHWInterface::Invalidate()\n");
	return InvalidateRegion(BRegion(frame));
}
