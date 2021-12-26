#ifndef _VIDEONODE_H_
#define _VIDEONODE_H_

#include <Handler.h>
#include <Messenger.h>
#include <private/app/MessengerPrivate.h>
#include <private/shared/AutoDeleter.h>

#include <VideoBuffer.h>


enum {
	videoNodeConnectMsg          = 1,
	videoNodeRequestSwapChainMsg = 2,
	videoNodeSwapChainChangedMsg = 3,
	videoNodePresentMsg          = 4,
	videoNodePresentedMsg        = 5,

	videoNodeInternalLastMsg     = 31,

	videoNodeLastMsg             = 1023,
};


class _EXPORT VideoNode: public BHandler
{
private:
	bool fIsConnected;
	BMessenger fLink;
	bool fSwapChainValid, fOwnsSwapChain;
	SwapChain fSwapChain;
	ArrayDeleter<VideoBuffer> fBuffers;

public:
	VideoNode(const char* name = NULL);
	virtual ~VideoNode();

	bool IsConnected() const {return fIsConnected;}
	const BMessenger& Link() const {return fLink;}
	status_t ConnectTo(const BMessenger& link);
	virtual void Connected(bool isActive);

	bool SwapChainValid() const {return fSwapChainValid;}
	bool OwnsSwapChain() const {return fOwnsSwapChain;}
	const SwapChain& GetSwapChain() const {return fSwapChain;}
	status_t SetSwapChain(const SwapChain* swapChain);
	status_t RequestSwapChain(const SwapChainSpec& spec);
	virtual status_t SwapChainRequested(const SwapChainSpec& spec);
	virtual void SwapChainChanged(bool isValid);

	void MessageReceived(BMessage* msg) override;
};


void WriteMessenger(const BMessenger& obj);


#endif	// _VIDEONODE_H_
