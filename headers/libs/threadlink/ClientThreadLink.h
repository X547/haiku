#pragma once

#include <PortLink.h>
#include <AutoDeleterOS.h>
#include <Messenger.h>
#include <pthread.h>
#include <util/DoublyLinkedList.h>


enum {
	quitServerMsg =     1,
	connectMsg    =     2,
	disconnectMsg =     3,
	userMsgBase   = 0x100,
};


class ClientThreadLinkConnection;

class ClientThreadLink {
private:
	friend class ThreadLinkHolder;

	ClientThreadLinkConnection *fConn;
	BPrivate::LinkSender fSender;
	BPrivate::LinkReceiver fReceiver;
	PortDeleter fPort;
	DoublyLinkedListLink<ClientThreadLink> fListLink;

public:
	typedef DoublyLinkedList<
		ClientThreadLink,
		DoublyLinkedListMemberGetLink<ClientThreadLink, &ClientThreadLink::fListLink>
	> List;

public:
	ClientThreadLink(ClientThreadLinkConnection *conn, const BMessenger &serverMsgr);
	~ClientThreadLink();
};


class ClientThreadLinkConnection {
private:
	friend class ClientThreadLink;
	friend class ThreadLinkHolder;

	pthread_mutex_t fLock = PTHREAD_MUTEX_INITIALIZER;
	BMessenger fServerMsgr;
	pthread_key_t fLinkTls;
	ClientThreadLink::List fLinks;

public:
	ClientThreadLinkConnection();
	~ClientThreadLinkConnection();
	const BMessenger &Messenger() const {return fServerMsgr;}
	void SetMessenger(const BMessenger &serverMsgr);
};


class ThreadLinkHolder: public BPrivate::ServerLink {
private:
	ClientThreadLink *fLink;

public:
	ThreadLinkHolder(ClientThreadLinkConnection &conn);
	~ThreadLinkHolder();
};
