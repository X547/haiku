#pragma once

#include "ClientThreadLink.h"


class ServerThreadLink {
private:
	BPrivate::PortLink fLink;
	PortDeleter fPort;
	team_id fClientTeam = -1;
	thread_id fThread = -1;

	status_t ThreadEntry();

public:
	ServerThreadLink(port_id clientPort);
	virtual ~ServerThreadLink();
	void Start();

	inline BPrivate::PortLink &Link() {return fLink;}
	inline team_id ClientTeam() {return fClientTeam;}
	void Close();

	virtual void MessageReceived(int32 what);
};
