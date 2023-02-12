#include "ServerThreadLink.h"
#include <stdio.h>

static void Check(status_t res) {if (res < B_OK) abort();}


ServerThreadLink::ServerThreadLink(port_id clientPort)
{
	fPort.SetTo(create_port(100, "server"));
	Check(fPort.Get());

	port_info portInfo;
	Check(get_port_info(clientPort, &portInfo));
	fClientTeam = portInfo.team;

	fLink.SetTo(clientPort, fPort.Get());
}

ServerThreadLink::~ServerThreadLink()
{
	fprintf(stderr, "-ServerThreadLink(), thread: %" B_PRId32 ", team: %" B_PRId32 "\n", fThread, fClientTeam);
}

void ServerThreadLink::Start()
{
	fThread = spawn_thread(
		[] (void *arg) {return ((ServerThreadLink*)arg)->ThreadEntry();},
		"client thread",
		B_NORMAL_PRIORITY,
		this
	);
	fprintf(stderr, "+ServerThreadLink(), thread: %" B_PRId32 ", team: %" B_PRId32 "\n", fThread, fClientTeam);
	Check(fThread);
	resume_thread(fThread);

	fLink.StartMessage(B_OK);
	fLink.Attach<int32>(fPort.Get());
	fLink.Flush();
}

void ServerThreadLink::Close()
{
	BPrivate::PortLink link(fPort.Get(), -1);
	link.StartMessage(disconnectMsg);
	link.Flush();
	close_port(fPort.Get());
}

void ServerThreadLink::MessageReceived(int32 what)
{
	(void)what;
	if (fLink.NeedsReply()) {
		fprintf(stderr, "[!] ServerThreadLink: unhandled message: %" B_PRId32 ", thread: %" B_PRId32 ", team: %" B_PRId32 "\n", what, fThread, fClientTeam);
		fLink.StartMessage(ENOSYS);
		fLink.Flush();
	}
}

status_t ServerThreadLink::ThreadEntry()
{
	for (;;) {
		int32 what;
		fLink.GetNextMessage(what);
		//fprintf(stderr, "thread message received: %" B_PRId32 "\n", what);
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
