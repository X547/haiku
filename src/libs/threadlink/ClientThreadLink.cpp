#include "ClientThreadLink.h"
#include <stddef.h>
#include <stdio.h>


thread_local ObjectDeleter<ClientThreadLink> tlsClientThreadLink = NULL;


ClientThreadLink::ClientThreadLink(port_id serverPort):
	fPort(create_port(100, "client"))
{
	printf("+ThreadLink(), thread: %" B_PRId32 "\n", find_thread(NULL));
	fLink.SetTo(serverPort, fPort.Get());
	int32 replyCode;
	port_id serverThreadPort;
	fLink.StartMessage(connectMsg);
	fLink.Attach<int32>(fPort.Get());
	fLink.FlushWithReply(replyCode);
	fLink.Read<int32>(&serverThreadPort);
	fLink.SetTo(serverThreadPort, fPort.Get());
}

ClientThreadLink::ClientThreadLink(const BMessenger &serverMsgr):
	fPort(create_port(100, "client"))
{
	printf("+ThreadLink(), thread: %" B_PRId32 "\n", find_thread(NULL));
	fLink.SetTo(-1, fPort.Get());
	int32 replyCode;
	port_id serverThreadPort;
	BMessage msg(connectMsg);
	msg.AddInt32("port", fPort.Get()); 
	serverMsgr.SendMessage(&msg);
	fLink.GetNextMessage(replyCode);
	fLink.Read<int32>(&serverThreadPort);
	fLink.SetTo(serverThreadPort, fPort.Get());
}

ClientThreadLink::~ClientThreadLink()
{
	printf("-ThreadLink(), thread: %" B_PRId32 "\n", find_thread(NULL));
	fLink.StartMessage(disconnectMsg);
	fLink.Flush();
}


ClientThreadLink *GetClientThreadLink(port_id serverPort)
{
	if (!tlsClientThreadLink.IsSet()) {
		ClientThreadLink *threadLink = new ClientThreadLink(serverPort);
		tlsClientThreadLink.SetTo(threadLink);
	}
	return tlsClientThreadLink.Get();
}

ClientThreadLink *GetClientThreadLink(const BMessenger &serverMsgr)
{
	if (!tlsClientThreadLink.IsSet()) {
		ClientThreadLink *threadLink = new ClientThreadLink(serverMsgr);
		tlsClientThreadLink.SetTo(threadLink);
	}
	return tlsClientThreadLink.Get();
}
