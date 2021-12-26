#include "VideoNode.h"

#include <stdio.h>


inline int32 ReplaceError(int32 err, int32 replaceWith) {return err < B_OK ? replaceWith : err;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}
#define CheckReply(msg, err) {status_t _err = (err); if (_err < B_OK) {BMessage reply(B_REPLY); reply.AddInt32("error", _err); msg->SendReply(&reply); return;}}


static status_t SendMessageSync(
	BHandler* src, const BMessenger &dst, BMessage* message, BMessage* reply,
	bigtime_t deliveryTimeout = B_INFINITE_TIMEOUT, bigtime_t replyTimeout = B_INFINITE_TIMEOUT
)
{
	if (dst.IsTargetLocal()) {
		BLooper* dstLooper;
		BHandler* dstHandler = dst.Target(&dstLooper);
		if (src->Looper() == dstLooper) {
			// !!! can't get reply
			dstHandler->MessageReceived(message);
			return B_OK;
		}
	}
	return dst.SendMessage(message, reply, deliveryTimeout, replyTimeout);
}


VideoNode::VideoNode(const char* name):
	BHandler(name),
	fIsConnected(false),
	fSwapChainValid(false),
	fOwnsSwapChain(false)
{
	memset(&fSwapChain, 0, sizeof(SwapChain));
}

VideoNode::~VideoNode()
{
	if (ConnectTo(BMessenger()) < B_OK)
		printf("can't disconnect\n");
}


status_t VideoNode::ConnectTo(const BMessenger& link)
{
	printf("VideoNode::ConnectTo("); WriteMessenger(BMessenger(this)); printf(", "); WriteMessenger(link); printf(")\n");
	if (Link() == link)
		return B_OK;

	if (IsConnected()) {
		BMessage reply;
		BMessage msg(videoNodeConnectMsg);
		msg.AddBool("doConnect", false);
		CheckRet(SendMessageSync(this, Link(), &msg, &reply));
		fLink = BMessenger();
		fIsConnected = false;
		Connected(false);
	}
	if (link.IsValid()) {
/*
		struct LinkReleaser {
			VideoNode& base;
			bool doRelease;
			LinkReleaser(VideoNode& base): base(base), doRelease(true) {}
			void Detach() {doRelease = false;}
			~LinkReleaser()
			{
				if (doRelease) {
					base.fLink = BMessenger();
					base.fIsConnected = false;
				}
			}
		} linkReleaser(*this);
*/
		BMessage reply;
		BMessage msg(videoNodeConnectMsg);
		msg.AddBool("doConnect", true);
		msg.AddMessenger("link", BMessenger(this));
		CheckRet(SendMessageSync(this, link, &msg, &reply));
		int32 error;
		if (reply.FindInt32("error", &error) >= B_OK)
			CheckRet(error);

//		linkReleaser.Detach();
		fLink = link;
		fIsConnected = true;
		Connected(true);
	}
	return B_OK;
}

void VideoNode::Connected(bool isActive)
{}


status_t VideoNode::SetSwapChain(const SwapChain* swapChain)
{
	printf("VideoNode::SetSwapChain()\n");

	if (swapChain == NULL) {
		if (!SwapChainValid())
			return B_OK;
		if (!OwnsSwapChain())
			return B_NOT_ALLOWED;

		fBuffers.Unset();
		fSwapChainValid = false;
		fOwnsSwapChain = false;

		if (IsConnected()) {
			BMessage msg(videoNodeSwapChainChangedMsg);
			msg.AddBool("isValid", false);
			BMessage reply;
			CheckRet(SendMessageSync(this, Link(), &msg, &reply));
		}

		SwapChainChanged(false);
		return B_OK;
	}

	if (swapChain->size != sizeof(SwapChain))
		return B_BAD_VALUE;

	memcpy(&fSwapChain, swapChain, swapChain->size);
	fBuffers.SetTo(new VideoBuffer[fSwapChain.bufferCnt]);
	fSwapChain.buffers = fBuffers.Get();
	memcpy(fSwapChain.buffers, swapChain->buffers, fSwapChain.bufferCnt*sizeof(VideoBuffer));

	fSwapChainValid = true;
	fOwnsSwapChain = true;

	printf("  swapChain: \n");
	printf("    size: %" B_PRIuSIZE "\n", fSwapChain.size);
	printf("    bufferCnt: %" B_PRIu32 "\n", fSwapChain.bufferCnt);

	if (IsConnected()) {
		BMessage msg(videoNodeSwapChainChangedMsg);
		msg.AddBool("isValid", true);
		msg.AddData("swapChain", B_RAW_TYPE, &fSwapChain, sizeof(VideoBuffer) - sizeof(void*));
		msg.AddData("buffers", B_RAW_TYPE, swapChain->buffers, fSwapChain.bufferCnt*sizeof(VideoBuffer));
		BMessage reply;
		CheckRet(SendMessageSync(this, Link(), &msg, &reply));
	}

	SwapChainChanged(true);
	return B_OK;
}

status_t VideoNode::RequestSwapChain(const SwapChainSpec& spec)
{
	printf("VideoNode::RequestSwapChain()\n");
	printf("  Link(): "); WriteMessenger(Link()); printf("\n");

	if (!IsConnected())
		return B_ERROR;

	BMessage msg(videoNodeRequestSwapChainMsg);
	msg.AddData("spec", B_RAW_TYPE, &spec, sizeof(SwapChainSpec) - sizeof(void*));
	msg.AddData("bufferSpecs", B_RAW_TYPE, spec.bufferSpecs, spec.bufferCnt*sizeof(BufferSpec));
	Link().SendMessage(&msg);

	return B_OK;
}


status_t VideoNode::SwapChainRequested(const SwapChainSpec& spec)
{
	return B_NOT_ALLOWED;
}

void VideoNode::SwapChainChanged(bool isValid)
{
}


void VideoNode::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case videoNodeConnectMsg: {
		bool doConnect;
		CheckReply(msg, ReplaceError(msg->FindBool("doConnect", &doConnect), B_BAD_VALUE));
		if (doConnect) {
			if (IsConnected())
				CheckReply(msg, B_ERROR); // already connected
			CheckReply(msg, ReplaceError(msg->FindMessenger("link", &fLink), B_BAD_VALUE));
			if (!fLink.IsValid())
				CheckReply(msg, B_ERROR);
		} else {
			if (!IsConnected())
				CheckReply(msg, B_ERROR); // not connected
			fLink = BMessenger();
		}
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		fIsConnected = doConnect;
		if (!IsConnected() && SwapChainValid() && !OwnsSwapChain()) {
			fBuffers.Unset();
			fSwapChainValid = false;
			fOwnsSwapChain = false;
			SwapChainChanged(false);
		}
		Connected(doConnect);
		return;
	}
	case videoNodeRequestSwapChainMsg: {
		printf("videoNodeRequestSwapChainMsg\n");
		const void* data;
		ssize_t size;
		SwapChainSpec spec;
		BufferSpec* bufferSpecs;
		CheckReply(msg, ReplaceError(msg->FindData("spec", B_RAW_TYPE, &data, &size), B_BAD_VALUE));
		if ((size_t)size < sizeof(SwapChainSpec) - sizeof(void*))
			CheckReply(msg, B_BAD_VALUE);
		memcpy(&spec, data, sizeof(SwapChainSpec) - sizeof(void*));
		CheckReply(msg, ReplaceError(msg->FindData("bufferSpecs", B_RAW_TYPE, (const void**)&bufferSpecs, &size), B_BAD_VALUE));
		if ((size_t)size < spec.bufferCnt*sizeof(BufferSpec))
			CheckReply(msg, B_BAD_VALUE);
		spec.bufferSpecs = bufferSpecs;
		CheckReply(msg, SwapChainRequested(spec));
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		// SwapChainRequested(spec);
		return;
	}
	case videoNodeSwapChainChangedMsg: {
		printf("videoNodeSwapChainChangedMsg\n");
		bool isValid;
		CheckReply(msg, ReplaceError(msg->FindBool("isValid", &isValid), B_BAD_VALUE));
		if (isValid) {
			const void* data;
			ssize_t size;
			SwapChain swapChain;
			VideoBuffer* buffers;
			CheckReply(msg, ReplaceError(msg->FindData("swapChain", B_RAW_TYPE, &data, &size), B_BAD_VALUE));
			if ((size_t)size < sizeof(SwapChain) - sizeof(void*))
				CheckReply(msg, B_BAD_VALUE);
			memcpy(&swapChain, data, sizeof(SwapChain) - sizeof(void*));
			CheckReply(msg, ReplaceError(msg->FindData("buffers", B_RAW_TYPE, (const void**)&buffers, &size), B_BAD_VALUE));
			if ((size_t)size < swapChain.bufferCnt*sizeof(VideoBuffer))
				CheckReply(msg, B_BAD_VALUE);
			swapChain.buffers = buffers;

			memcpy(&fSwapChain, &swapChain, sizeof(SwapChain));
			fBuffers.SetTo(new VideoBuffer[fSwapChain.bufferCnt]);
			fSwapChain.buffers = fBuffers.Get();
			memcpy(fSwapChain.buffers, swapChain.buffers, fSwapChain.bufferCnt*sizeof(VideoBuffer));

			fSwapChainValid = true;
		} else {
			fBuffers.Unset();
			fSwapChainValid = false;
		}

		SwapChainChanged(isValid);
		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
	}
	}
	BHandler::MessageReceived(msg);
}


void _EXPORT WriteMessenger(const BMessenger& obj)
{
	printf("(team: %" B_PRId32 ", port: %" B_PRId32 ", token: %" B_PRId32 ")",
		BMessenger::Private((BMessenger*)&obj).Team(),
		BMessenger::Private((BMessenger*)&obj).Port(),
		BMessenger::Private((BMessenger*)&obj).Token()
	);
}
