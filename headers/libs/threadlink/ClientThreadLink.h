#pragma once

#include <PortLink.h>
#include <AutoDeleterOS.h>
#include <Messenger.h>


enum {
	quitServerMsg =     1,
	connectMsg    =     2,
	disconnectMsg =     3,
	userMsgBase   = 0x100,
};


class ClientThreadLink {
private:
	BPrivate::PortLink fLink;
	PortDeleter fPort;

public:
	ClientThreadLink(port_id serverPort);
	ClientThreadLink(const BMessenger &serverMsgr);
	~ClientThreadLink();

	inline BPrivate::PortLink &Link() {return fLink;}
};


ClientThreadLink *GetClientThreadLink(port_id serverPort);
ClientThreadLink *GetClientThreadLink(const BMessenger &serverMsgr);
