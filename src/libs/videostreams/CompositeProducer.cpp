#include "CompositeProducer.h"

#include "CompositeConsumer.h"

#include <Looper.h>
#include <Application.h>

#include <stdio.h>


inline int32 ReplaceError(int32 err, int32 replaceWith) {return err < B_OK ? replaceWith : err;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}
#define CheckReply(msg, err) {status_t _err = (err); if (_err < B_OK) {BMessage reply(B_REPLY); reply.AddInt32("error", _err); msg->SendReply(&reply); return;}}
static void Assert(bool cond) {if (!cond) abort();}


/*
Surface* BuildTestSurface(BRect rect)
{
	Surface* surf = new Surface();
	surf->frame = rect;
	surf->drawMode = B_OP_ALPHA;
	surf->displayRb.width = (int32)rect.Width() + 1;
	surf->displayRb.height = (int32)rect.Height() + 1;
	surf->displayRb.stride = surf->displayRb.width;
	surf->displayRbMem.SetTo(new uint8[surf->displayRb.width*surf->displayRb.height*4]);
	surf->displayRb.colors = (uint32*)surf->displayRbMem.Get();

	for (int32 y = 0; y < surf->displayRb.height; y++) {
			for (int32 x = 0; x < surf->displayRb.width; x++) {
				*(surf->displayRb.colors + y*surf->displayRb.stride + x) = 0xcc000000 + x%0x100 + y%0x100*0x100;
			}
	}
	
	return surf;
}
*/

CompositeProducer::CompositeProducer(const char* name):
	VideoProducer(name), fUpdateRequested(false)
{
/*
	for (int i = 0; i < 4; i++) {
		fSurfaces.Insert(BuildTestSurface(BRect(0, 0, 255, 255).OffsetByCopy(32 + 64*i, 32 + 48*i)));
	}	
*/
}

CompositeProducer::~CompositeProducer()
{
	while (Surface* surf = fSurfaces.RemoveHead()) {
		delete surf;
	}
}


static void FillRegion(const RasBuf32 &rb, const BRegion& region, uint32 color)
{
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect rect = region.RectAtInt(i);
		rb.Clip2(rect.left, rect.top, rect.right + 1, rect.bottom + 1).Clear(color);
	}
}

static void DrawSurface(const RasBuf32& dst, Surface* surf, const BRegion& clipping)
{
	RasBuf32 displayRb = surf->consumer->DisplayRasBuf();
	if (displayRb.colors == NULL) {
		return;
	}
	switch (surf->drawMode) {
	case B_OP_COPY: {
//			(RasBufOfs<uint32>(dst) + surf->frame.LeftTop()).Blit(displayRb);
		for (int32 i = 0; i < clipping.CountRects(); i++) {
			(RasBufOfs<uint32>(dst).ClipOfs(clipping.RectAt(i)) + surf->frame.LeftTop()).Blit(displayRb);
		}
		break;
	}
	case B_OP_ALPHA:
		for (int32 i = 0; i < clipping.CountRects(); i++) {
			(RasBufOfs<uint32>(dst).ClipOfs(clipping.RectAt(i)) + surf->frame.LeftTop()).BlitRgb(displayRb);
		}
		break;
	default:
		;
	}
}

void CompositeProducer::Restore(int32 bufferId, const BRegion& dirty)
{
	//printf("CompositeProducer::Restore((%" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 "))\n", dirty.FrameInt().left, dirty.FrameInt().top, dirty.FrameInt().right, dirty.FrameInt().bottom);
	
	RasBuf32 dst = GetRasBuf(bufferId);

	FillRegion(dst, dirty, 0xffcccccc);

	for (DoublyLinkedList<Surface>::Iterator it = fSurfaces.GetIterator(); Surface* surf = it.Next(); ) {
		if (surf->clippingEnabled) {
			BRegion clipping = surf->clipping;
			clipping.OffsetBy(surf->frame.LeftTop());
			clipping.IntersectWith(&dirty);
			DrawSurface(dst, surf, clipping);
		} else {
			DrawSurface(dst, surf, dirty);
		}
	}
}


void CompositeProducer::Connected(bool isActive)
{
	if (isActive) {
		printf("TestProducer: connected to ");
		WriteMessenger(Link());
		printf("\n");

		UpdateSwapChain(0, 0);
	} else {
		printf("CompositeProducer: disconnected\n");
		be_app_messenger.SendMessage(B_QUIT_REQUESTED);
	}
}

void CompositeProducer::UpdateSwapChain(int32 width, int32 height)
{
		SwapChainSpec spec {
			.size = sizeof(SwapChainSpec),
			.presentEffect = presentEffectSwap,
			.bufferCnt = 2,
			.kind = bufferRefArea,
			.width = width,
			.height = height,
			.colorSpace = B_RGBA32
		};
		if (RequestSwapChain(spec) < B_OK) {
			printf("[!] can't request swap chain\n");
			exit(1);
		}
		fSwapChainChanging = true;
}

void CompositeProducer::SwapChainChanged(bool isValid)
{
	VideoProducer::SwapChainChanged(isValid);
	printf("TestProducer::SwapChainChanged(%d)\n", isValid);

	fSwapChainBind.Unset();

	if (!isValid) {
		return;
	}
	DumpSwapChain(GetSwapChain());

	fSwapChainBind.ConnectTo(GetSwapChain());

	fValidPrevBufCnt = 0;

	Assert(fPending == 0);
	fSwapChainChanging = false;
	Produce();
}

void CompositeProducer::Presented(const PresentedInfo &presentedInfo)
{
	fPending--;
	if (presentedInfo.suboptimal) {
		if (fPending == 0) {
			UpdateSwapChain(presentedInfo.width, presentedInfo.height);
		}
	} else {
		if (fPending == 0 && !fSwapChainChanging && fDirty.CountRects() > 0) {
			Produce();
		}
	}
	VideoProducer::Presented(presentedInfo);
}


void CompositeProducer::Produce()
{
	if (!SwapChainValid()) return;

	int32 bufferId = AllocBuffer();

	if (bufferId < 0) {
		printf("[!] AllocBuffer failed\n");
		return;
	}

	switch (GetSwapChain().presentEffect) {
		case presentEffectSwap: {
			BRegion dirty;
			dirty = fDirty; fDirty.MakeEmpty();
			BRegion combinedDirty(dirty);
			if (fValidPrevBufCnt < 2) {
				const VideoBuffer& buf = GetSwapChain().buffers[bufferId];
				combinedDirty.Set(BRect(0, 0, buf.format.width - 1, buf.format.height - 1));
				fValidPrevBufCnt++;
			} else {
				combinedDirty.Include(&fPrevDirty);
			}
			Restore(bufferId, combinedDirty);
			fPrevDirty = dirty;
			fPending++;
			Present(bufferId, fValidPrevBufCnt == 1 ? &combinedDirty : &dirty);
			break;
		}
		case presentEffectCopy:
		default: {
			BRegion dirty;
			dirty = fDirty; fDirty.MakeEmpty();
			if (fValidPrevBufCnt < 1) {
				const VideoBuffer& buf = *RenderBuffer();
				dirty.Set(BRect(0, 0, buf.format.width - 1, buf.format.height - 1));
				fValidPrevBufCnt++;
			}
			Restore(bufferId, dirty);
			fPending++;
			Present(bufferId, &dirty);
			break;
		}
	}
}


CompositeConsumer* CompositeProducer::NewSurface(const char* name, const SurfaceUpdate& update)
{
	Surface* surf = new Surface();
	surf->frame = BRect();
	surf->clippingEnabled = false;
	surf->drawMode = B_OP_COPY;

	surf->consumer.SetTo(new CompositeConsumer(name, this, surf));
	Looper()->AddHandler(surf->consumer.Get());

	fSurfaces.Insert(surf);

	UpdateSurface(surf->consumer.Get(), update);

	return surf->consumer.Get();
}

status_t CompositeProducer::DeleteSurface(CompositeConsumer* cons)
{
	Surface* surf = cons->GetSurface();
	fSurfaces.Remove(surf);
	delete surf;
	return B_OK;
}

void CompositeProducer::GetSurface(CompositeConsumer* cons, SurfaceUpdate& update)
{
	Surface* surf = cons->GetSurface();
	if (((1 << surfaceFrame) & update.valid) != 0) {
		update.frame = surf->frame;
	}
	if (((1 << surfaceClipping) & update.valid) != 0) {
		if (surf->clippingEnabled) {
			update.clipping = &surf->clipping;
		} else {
			update.clipping = NULL;
		}
	}
	if (((1 << surfaceDrawMode) & update.valid) != 0) {
		update.drawMode = surf->drawMode;
	}
}

void CompositeProducer::UpdateSurface(CompositeConsumer* cons, const SurfaceUpdate& update)
{
	//printf("UpdateSurface\n");
	//printf("  valid: %#" B_PRIx32 "\n", update.valid);
	Surface* surf = cons->GetSurface();
	if (((1 << surfaceFrame) & update.valid) != 0) {
		//printf("  frame: "); update.frame.PrintToStream();
		Invalidate(surf->frame);
		surf->frame = update.frame;
		Invalidate(surf->frame);
	}
	if (((1 << surfaceClipping) & update.valid) != 0) {
		//printf("  clipping: "); if (update.clipping == NULL) printf("NULL\n"); else update.clipping->PrintToStream();
		if (update.clipping != NULL) {
			surf->clippingEnabled = true;
			surf->clipping = *update.clipping;
		} else {
			surf->clippingEnabled = false;
			surf->clipping.MakeEmpty();
		}
	}
	if (((1 << surfaceDrawMode) & update.valid) != 0) {
		//printf("  drawMode: %" B_PRId32 "\n", update.drawMode);
		surf->drawMode = update.drawMode;
	}
}

void CompositeProducer::InvalidateSurface(CompositeConsumer* cons, const BRegion* dirty)
{
	Surface* surf = cons->GetSurface();

	if (dirty != NULL) {
		BRegion region = *dirty;
		if (surf->clippingEnabled) {
			region.IntersectWith(&surf->clipping);
		}
		region.OffsetBy(surf->frame.LeftTop());
		BRegion frameRegion(surf->frame);
		region.IntersectWith(&frameRegion);
		Invalidate(region);
	} else {
		if (surf->clippingEnabled) {
			BRegion region = surf->clipping;
			region.OffsetBy(surf->frame.LeftTop());
			Invalidate(region);
		} else {
			Invalidate(surf->frame);
		}
	}
}


void CompositeProducer::Invalidate(const BRect rect)
{
	Invalidate(BRegion(rect));
}

void CompositeProducer::Invalidate(const BRegion& region)
{
	//printf("CompositeProducer::Invalidate((%" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 ", %" B_PRId32 "))\n", region.FrameInt().left, region.FrameInt().top, region.FrameInt().right, region.FrameInt().bottom);
	fDirty.Include(&region);
	if (fDirty.CountRects() > 0 && !fUpdateRequested && !fSwapChainChanging && fPending == 0) {
		fUpdateRequested = true;
		BMessenger(this).SendMessage(stepMsg);
	}
}


void CompositeProducer::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case stepMsg: {
		fUpdateRequested = false;
		if (fPending == 0 && !fSwapChainChanging) {
			Produce();
		}
		return;
	}

	case compositeProducerNewSurfaceMsg: {
		const char* name;
		BRegion regionMem;
		SurfaceUpdate update = {.clipping = &regionMem};
		CheckReply(msg, msg->FindString("name", &name));
		CheckReply(msg, GetSurfaceUpdate(*msg, update));
		CompositeConsumer* cons = NewSurface(name, update);
		if (cons == NULL) CheckReply(msg, B_NO_MEMORY);
		BMessage reply(B_REPLY);
		reply.AddMessenger("cons", BMessenger(cons));
		msg->SendReply(&reply);
		return;
	}
	case compositeProducerDeleteSurfaceMsg: {
		BMessenger consMsgr;
		CheckReply(msg, msg->FindMessenger("cons", &consMsgr));
		CompositeConsumer* cons = dynamic_cast<CompositeConsumer*>(consMsgr.Target(NULL));
		if (cons == NULL) CheckReply(msg, B_BAD_VALUE);
		CheckReply(msg, DeleteSurface(cons));
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}
	case compositeProducerGetSurfaceMsg: {
		BMessenger consMsgr;
		SurfaceUpdate update;
		CheckReply(msg, msg->FindMessenger("cons", &consMsgr));
		CompositeConsumer* cons = dynamic_cast<CompositeConsumer*>(consMsgr.Target(NULL));
		if (cons == NULL) CheckReply(msg, B_BAD_VALUE);
		CheckReply(msg, msg->FindUInt32("valid", &update.valid));
		GetSurface(cons, update);
		BMessage reply(B_REPLY);
		CheckReply(msg, SetSurfaceUpdate(reply, update));
		msg->SendReply(&reply);
		return;
	}
	case compositeProducerUpdateSurfaceMsg: {
		BMessenger consMsgr;
		BRegion regionMem;
		SurfaceUpdate update = {.clipping = &regionMem};
		CheckReply(msg, msg->FindMessenger("cons", &consMsgr));
		CompositeConsumer* cons = dynamic_cast<CompositeConsumer*>(consMsgr.Target(NULL));
		if (cons == NULL) CheckReply(msg, B_BAD_VALUE);
		CheckReply(msg, GetSurfaceUpdate(*msg, update));
		UpdateSurface(cons, update);
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}
	case compositeProducerInvalidateSurfaceMsg: {
		BMessenger consMsgr;
		BRegion dirtyMem;
		BRegion* dirty = &dirtyMem;
		CheckReply(msg, msg->FindMessenger("cons", &consMsgr));
		CompositeConsumer* cons = dynamic_cast<CompositeConsumer*>(consMsgr.Target(NULL));
		if (cons == NULL) CheckReply(msg, B_BAD_VALUE);
		CheckReply(msg, GetRegion(*msg, "dirty", dirty));
		InvalidateSurface(cons, dirty);
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}

	case compositeProducerInvalidateMsg: {
		BRegion dirtyMem;
		BRegion* dirty = &dirtyMem;
		CheckReply(msg, GetRegion(*msg, "dirty", dirty));
		if (dirty == NULL) CheckReply(msg, B_BAD_VALUE);
		Invalidate(*dirty);
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}
	}

	VideoProducer::MessageReceived(msg);
};


status_t GetSurfaceUpdate(BMessage& msg, SurfaceUpdate& update)
{
	update.valid = 0;
	if (msg.FindRect("frame", &update.frame) >= B_OK) {
		update.valid |= (1 << surfaceFrame);
	}
	if (GetRegion(msg, "clipping", update.clipping) >= B_OK) {
		update.valid |= (1 << surfaceClipping);
	}
	if (msg.FindInt32("drawMode", (int32*)&update.drawMode) >= B_OK) {
		update.valid |= (1 << surfaceDrawMode);
	}
	return B_OK;
}

status_t SetSurfaceUpdate(BMessage& msg, const SurfaceUpdate& update)
{
	if (((1 << surfaceFrame) & update.valid) != 0) {
		CheckRet(msg.AddRect("frame", update.frame));
	}
	if (((1 << surfaceClipping) & update.valid) != 0) {
		CheckRet(SetRegion(msg, "clipping", update.clipping));
	}
	if (((1 << surfaceDrawMode) & update.valid) != 0) {
		CheckRet(msg.AddInt32("drawMode", update.drawMode));
	}
	return B_OK;
}
