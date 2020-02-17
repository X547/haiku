/*
 * Copyright 2020 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef _CREATERAMDISKPANEL_H_
#define _CREATERAMDISKPANEL_H_


#include <Window.h>

class BTextControl;


class CreateRamDiskPanel : public BWindow {
public:
								CreateRamDiskPanel(BWindow* window);
	virtual						~CreateRamDiskPanel();

private:
	void				MessageReceived(BMessage* msg);

	BTextControl*		fPathControl;
	BTextControl*		fSizeControl;
};


#endif	// _CREATERAMDISKPANEL_H_
