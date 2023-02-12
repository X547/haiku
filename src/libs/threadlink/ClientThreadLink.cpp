#include "ClientThreadLink.h"
#include <PthreadMutexLocker.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <new>


ClientThreadLink::ClientThreadLink(ClientThreadLinkConnection *conn, const BMessenger &serverMsgr):
	fConn(conn), fSender(B_ERROR), fReceiver(B_ERROR), fPort(create_port(100, "client"))
{
	fprintf(stderr, "+ThreadLink(), thread: %" B_PRId32 "\n", find_thread(NULL));

	BMessage msg(connectMsg);
	msg.AddInt32("port", fPort.Get());
	serverMsgr.SendMessage(&msg);

	int32 replyCode;
	port_id serverThreadPort;
	fReceiver.SetPort(fPort.Get());
	fReceiver.GetNextMessage(replyCode);
	fReceiver.Read(&serverThreadPort);
	fSender.SetPort(serverThreadPort);
}

ClientThreadLink::~ClientThreadLink()
{
	fprintf(stderr, "-ThreadLink(), thread: %" B_PRId32 "\n", find_thread(NULL));
	fSender.StartMessage(disconnectMsg);
	fSender.Flush();
	PthreadMutexLocker lock(&fConn->fLock);
	fConn->fLinks.Remove(this);
}


ClientThreadLinkConnection::ClientThreadLinkConnection()
{
	if (pthread_key_create(&fLinkTls, [](void *arg) {
		delete static_cast<ClientThreadLink*>(arg);
	}) < 0) abort();
}

ClientThreadLinkConnection::~ClientThreadLinkConnection()
{
	PthreadMutexLocker lock(&fLock);
	while (ClientThreadLink *threadLink = fLinks.First()) {
		delete threadLink;
	}
	if (pthread_key_delete(fLinkTls) < 0) abort();
}

void ClientThreadLinkConnection::SetMessenger(const BMessenger &serverMsgr)
{
	fServerMsgr = serverMsgr;
}


ThreadLinkHolder::ThreadLinkHolder(ClientThreadLinkConnection &conn)
{
	ClientThreadLink *threadLink = (ClientThreadLink*)pthread_getspecific(conn.fLinkTls);
	if (threadLink == NULL) {
		PthreadMutexLocker lock(&conn.fLock);
		threadLink = new ClientThreadLink(&conn, conn.fServerMsgr);
		conn.fLinks.Insert(threadLink);
		pthread_setspecific(conn.fLinkTls, threadLink);
	}
	fSender = &threadLink->fSender;
	fReceiver = &threadLink->fReceiver;
}

ThreadLinkHolder::~ThreadLinkHolder()
{
	CancelMessage();
}
