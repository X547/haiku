#pragma once

#include "ClientThreadLink.h"


class ServerThreadLink {
private:
	BPrivate::PortLink fLink;
	PortDeleter fPort;
	team_id fClientTeam;
	thread_id fThread;
	
	status_t ThreadEntry();

public:
	ServerThreadLink(port_id clientPort);
	virtual ~ServerThreadLink();

	inline BPrivate::PortLink &Link() {return fLink;}
	inline team_id ClientTeam() {return fClientTeam;}
	void Close();

	virtual void MessageReceived(int32 what);
};

class ServerLinkWatcher {
private:
	BPrivate::PortLink fLink;
	ServerThreadLink *(*fFactory)(port_id clientPort);

public:
	ServerLinkWatcher(port_id serverPort, ServerThreadLink *(*Factory)(port_id clientPort));
	void Quit();
	void Run();
};

ServerThreadLink *GetServerThreadLink();
