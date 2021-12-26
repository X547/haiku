#include "ServerThreadLink.h"
#include <stdio.h>


thread_local ServerThreadLink *tlsServerThreadLink = NULL;


ServerThreadLink::ServerThreadLink(port_id clientPort):
	fPort(create_port(100, "server"))
{
	port_info portInfo;
	get_port_info(clientPort, &portInfo);
	fClientTeam = portInfo.team;

	fLink.SetTo(clientPort, fPort.Get());

	fLink.StartMessage(B_OK);
	fLink.Attach<int32>(fPort.Get());
	fLink.Flush();

	fThread = spawn_thread(
		[] (void *arg) {return ((ServerThreadLink*)arg)->ThreadEntry();},
		"client thread",
		B_NORMAL_PRIORITY,
		this
	);
	printf("+ServerThreadLink(), thread: %" B_PRId32 ", team: %" B_PRId32 "\n", fThread, fClientTeam);
	resume_thread(fThread);
}

ServerThreadLink::~ServerThreadLink()
{
	printf("-ServerThreadLink(), thread: %" B_PRId32 ", team: %" B_PRId32 "\n", fThread, fClientTeam);
}

void ServerThreadLink::Close()
{
	BPrivate::PortLink link(fPort.Get(), -1);
	link.StartMessage(disconnectMsg);
	link.Flush();
}

void ServerThreadLink::MessageReceived(int32 what)
{
	(void)what;
	if (fLink.NeedsReply()) {
		fLink.StartMessage(B_ERROR);
		fLink.Flush();
	}
}

status_t ServerThreadLink::ThreadEntry()
{
	for (;;) {
		int32 what;
		fLink.GetNextMessage(what);
		//printf("thread message received: %" B_PRId32 "\n", what);
		switch (what) {
			case disconnectMsg:
				if (fLink.NeedsReply()) {
					fLink.StartMessage(B_OK);
					fLink.Flush();
				}
				delete this;
				return B_OK;
				break;
			default:
				MessageReceived(what);
		}
	}
	return B_OK;
}


ServerThreadLink *GetServerThreadLink()
{
	return tlsServerThreadLink;
}


ServerLinkWatcher::ServerLinkWatcher(port_id serverPort, ServerThreadLink *(*Factory)(port_id clientPort)):
	fLink(-1, serverPort),
	fFactory(Factory)
{
}

void ServerLinkWatcher::Quit()
{
	printf("[!] ServerLinkWatcher::Quit: not implemented\n");
	abort();
}

void ServerLinkWatcher::Run()
{
	for (;;) {
		int32 what;
		fLink.GetNextMessage(what);
		switch (what) {
			case quitServerMsg: {
				return;
			}
			case connectMsg: {
				port_id replyPort;
				fLink.Read<int32>(&replyPort);
				BPrivate::LinkSender replySender(replyPort);
				fFactory(replyPort);
				break;
			}
		}
	}
}
