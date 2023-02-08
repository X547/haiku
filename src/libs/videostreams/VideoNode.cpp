#include "VideoNode.h"
#include "VideoBuffer.h"
#include <private/app/MessengerPrivate.h>

#include <stdio.h>


inline int32 ReplaceError(int32 err, int32 replaceWith) {return err < B_OK ? replaceWith : err;}
#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) {abort(); return _err;}}
#define CheckReply(msg, err) {status_t _err = (err); if (_err < B_OK) {BMessage reply(B_REPLY); reply.AddInt32("error", _err); msg->SendReply(&reply); return;}}


static VideoNode *GetLocalNode(VideoNode *node, const BMessenger &src)
{
	if (!src.IsValid() || !src.IsTargetLocal()) return NULL;
	BLooper* srcLooper = NULL;
	BHandler* srcHandler = src.Target(&srcLooper);
	if (node->Looper() != srcLooper) return NULL;
	return (VideoNode*)srcHandler;
}


//#pragma mark - VideoNodeProxy

VideoNodeRef::VideoNodeRef(VideoNode *node, const BMessenger& link):
	fNode(node), fProxy(link)
{}

VideoNodeProxyBase &VideoNodeRef::Get()
{
	VideoNode *linkPtr = GetLocalNode(fNode, Link());
	if (linkPtr != NULL) {
		return *linkPtr;
	}
	return fProxy;
}


status_t VideoNodeProxy::ConnectInt(bool doConnect, const BMessenger &link)
{
	BMessage reply;
	BMessage msg(videoNodeConnectMsg);
	msg.AddBool("doConnect", doConnect);
	if (doConnect) {
		msg.AddMessenger("link", link);
	}
	CheckRet(Link().SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK)
		CheckRet(error);
	return B_OK;
}

status_t VideoNodeProxy::SwapChainRequested(const SwapChainSpec& spec)
{
	BMessage msg(videoNodeRequestSwapChainMsg);
	msg.AddData("spec", B_RAW_TYPE, &spec, sizeof(SwapChainSpec));
	Link().SendMessage(&msg);
	return B_OK;
}

status_t VideoNodeProxy::SwapChainChangedInt(bool isValid, ObjectDeleter<SwapChain> &swapChain)
{
	BMessage msg(videoNodeSwapChainChangedMsg);
	msg.AddBool("isValid", isValid);
	if (isValid) {
		CheckRet(swapChain->ToMessage(msg, "swapChain", Link().Team()));
	}

	BMessage reply;
	CheckRet(Link().SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK)
		CheckRet(error);
	return B_OK;
}


//#pragma mark - VideoNode

VideoNode::VideoNode(const char* name):
	BHandler(name),
	fIsConnected(false),
	fLink(this, BMessenger()),
	fOwnsSwapChain(false)
{
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
		CheckRet(fLink.Get().ConnectInt(false, BMessenger()));
		ConnectInt(false, BMessenger());
	}
	if (link.IsValid()) {
		CheckRet(VideoNodeRef(this, link).Get().ConnectInt(true, BMessenger(this)));
		ConnectInt(true, link);
	}
	return B_OK;
}

void VideoNode::Connected(bool isActive)
{}


status_t VideoNode::SetSwapChain(const SwapChain* swapChain)
{
	printf("VideoNode::SetSwapChain(%p)\n", swapChain);
	if (swapChain != NULL) {
		DumpSwapChain(*swapChain);
	}

	if (swapChain == NULL) {
		if (!SwapChainValid())
			return B_OK;
		if (!OwnsSwapChain())
			return B_NOT_ALLOWED;

		ObjectDeleter<SwapChain> swapChainNull;
		if (IsConnected()) {
			CheckRet(fLink.Get().SwapChainChangedInt(false, swapChainNull));
		}
		CheckRet(SwapChainChangedInt(false, swapChainNull));
		return B_OK;
	}

	if (swapChain->size != sizeof(SwapChain))
		return B_BAD_VALUE;

	ObjectDeleter<SwapChain> swapChainCopy;
	CheckRet(swapChain->Copy(swapChainCopy));

	fOwnsSwapChain = true;

	if (IsConnected()) {
			// may move swapChainCopyto itself making it NULL
			CheckRet(fLink.Get().SwapChainChangedInt(true, swapChainCopy));
	}
	if (!swapChainCopy.IsSet()) {
		CheckRet(swapChain->Copy(swapChainCopy));
	}
	CheckRet(SwapChainChangedInt(true, swapChainCopy));

	return B_OK;
}

status_t VideoNode::RequestSwapChain(const SwapChainSpec& spec)
{
	printf("VideoNode::RequestSwapChain()\n");
	printf("  Link(): "); WriteMessenger(Link()); printf("\n");

	if (!IsConnected())
		return B_ERROR;

	return fLink.Get().SwapChainRequested(spec);
}


status_t VideoNode::SwapChainRequested(const SwapChainSpec& spec)
{
	return B_NOT_ALLOWED;
}

void VideoNode::SwapChainChanged(bool isValid)
{
}


status_t VideoNode::ConnectInt(bool doConnect, const BMessenger &link)
{
	if (doConnect) {
		if (IsConnected())
			return B_ERROR; // already connected
		if (!link.IsValid())
			return B_ERROR;
		fLink.SetLink(link);
	} else {
		if (!IsConnected())
			return B_ERROR; // not connected
		fLink.SetLink(BMessenger());
	}
	fIsConnected = doConnect;
	if (!IsConnected() && SwapChainValid() && !OwnsSwapChain()) {
		ObjectDeleter<SwapChain> swapChain;
		SwapChainChangedInt(false, swapChain);
	}
	Connected(doConnect);
	return B_OK;
}

status_t VideoNode::SwapChainChangedInt(bool isValid, ObjectDeleter<SwapChain> &swapChain)
{
	if (isValid) {
		fSwapChain.SetTo(swapChain.Detach());
	} else {
		fSwapChain.Unset();
		fOwnsSwapChain = false;
	}
	SwapChainChanged(isValid);
	return B_OK;
}


void VideoNode::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case videoNodeConnectMsg: {
		bool doConnect;
		BMessenger link;
		CheckReply(msg, ReplaceError(msg->FindBool("doConnect", &doConnect), B_BAD_VALUE));
		if (doConnect) {
			CheckReply(msg, ReplaceError(msg->FindMessenger("link", &link), B_BAD_VALUE));
		}
		CheckReply(msg, ConnectInt(doConnect, link));

		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}
	case videoNodeRequestSwapChainMsg: {
		printf("videoNodeRequestSwapChainMsg\n");
		const void* data;
		ssize_t size;
		SwapChainSpec spec;
		CheckReply(msg, ReplaceError(msg->FindData("spec", B_RAW_TYPE, &data, &size), B_BAD_VALUE));
		if ((size_t)size < sizeof(SwapChainSpec))
			CheckReply(msg, B_BAD_VALUE);
		memcpy(&spec, data, sizeof(SwapChainSpec));
		CheckReply(msg, SwapChainRequested(spec));

		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
	}
	case videoNodeSwapChainChangedMsg: {
		printf("videoNodeSwapChainChangedMsg\n");
		bool isValid;
		CheckReply(msg, ReplaceError(msg->FindBool("isValid", &isValid), B_BAD_VALUE));
		ObjectDeleter<SwapChain> swapChain;
		if (isValid) {
			CheckReply(msg, SwapChain::NewFromMessage(swapChain, *msg, "swapChain"));
		}
		CheckReply(msg, SwapChainChangedInt(isValid, swapChain));

		BMessage reply(B_REPLY);
		msg->SendReply(&reply);
		return;
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
