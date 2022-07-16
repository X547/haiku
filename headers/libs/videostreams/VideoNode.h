#ifndef _VIDEONODE_H_
#define _VIDEONODE_H_

#include <Handler.h>
#include <Messenger.h>
#include <private/shared/AutoDeleter.h>

class SwapChain;
class SwapChainSpec;


enum {
	videoNodeConnectMsg          = 1,
	videoNodeRequestSwapChainMsg = 3,
	videoNodeSwapChainChangedMsg = 4,
	videoNodePresentMsg          = 5,
	videoNodePresentedMsg        = 6,
	videoNodeGetConsumerInfoMsg  = 7,
	videoNodeAllocBufferMsg      = 8,

	videoNodeInternalLastMsg     = 31,

	videoNodeLastMsg             = 1023,
};


class VideoNodeProxyBase {
public:
	virtual status_t ConnectInt(bool doConnect, const BMessenger &link) = 0;
	virtual status_t SwapChainRequested(const SwapChainSpec& spec) = 0;
	virtual status_t SwapChainChangedInt(bool isValid, ObjectDeleter<SwapChain> &swapChain) = 0;
};

class VideoNodeProxy: public VideoNodeProxyBase {
private:
	BMessenger fLink;

public:
	VideoNodeProxy(const BMessenger& link): fLink(link) {}
	const BMessenger &Link() const {return fLink;}
	void SetLink(const BMessenger& link) {fLink = link;}

	status_t ConnectInt(bool doConnect, const BMessenger &link) final;
	status_t SwapChainRequested(const SwapChainSpec& spec) final;
	status_t SwapChainChangedInt(bool isValid, ObjectDeleter<SwapChain> &swapChain) final;
};

class VideoNode;

class VideoNodeRef {
private:
	VideoNode *fNode;
	VideoNodeProxy fProxy;

public:
	VideoNodeRef(VideoNode *node, const BMessenger& link);
	const BMessenger &Link() const {return fProxy.Link();}
	void SetLink(const BMessenger& link) {fProxy.SetLink(link);}

	VideoNodeProxyBase &Get();
};

class _EXPORT VideoNode: public BHandler, public VideoNodeProxyBase
{
private:
	bool fIsConnected;
	VideoNodeRef fLink;
	bool fOwnsSwapChain;
	ObjectDeleter<SwapChain> fSwapChain;

	status_t ConnectInt(bool doConnect, const BMessenger &link) final;
	status_t SwapChainChangedInt(bool isValid, ObjectDeleter<SwapChain> &swapChain) final;

public:
	struct ConsumerInfo {
		int32 width;
		int32 height;
	};

	struct PresentedInfo {
		uint32 era;
		bool suboptimal;
		int32 width;
		int32 height;
	};

	VideoNode(const char* name = NULL);
	virtual ~VideoNode();

	bool IsConnected() const {return fIsConnected;}
	const BMessenger& Link() const {return fLink.Link();}
	status_t ConnectTo(const BMessenger& link);

	bool SwapChainValid() const {return fSwapChain.IsSet();}
	bool OwnsSwapChain() const {return fOwnsSwapChain;}
	const SwapChain& GetSwapChain() const {return *fSwapChain.Get();}
	status_t SetSwapChain(const SwapChain* swapChain);
	status_t RequestSwapChain(const SwapChainSpec& spec);

	virtual void Connected(bool isActive);
	virtual status_t SwapChainRequested(const SwapChainSpec& spec);
	virtual void SwapChainChanged(bool isValid);

	void MessageReceived(BMessage* msg) override;
};


void WriteMessenger(const BMessenger& obj);
void DumpSwapChain(const SwapChain &swapChain);


#endif	// _VIDEONODE_H_
