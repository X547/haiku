/*
 * Copyright 2020 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 */


#include "CreateRamDiskPanel.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <Button.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "CreateRamDiskPanel"


static const uint32 kMsgOk = 'okok';


CreateRamDiskPanel::CreateRamDiskPanel(BWindow* window)
	:
	BWindow(BRect(300.0, 200.0, 600.0, 300.0), "Create RAM disk", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fPathControl = new BTextControl("Path", B_TRANSLATE("Mount point:"), "", NULL);
	fSizeControl = new BTextControl("Size", B_TRANSLATE("Size:"), "", NULL);

	BLayoutBuilder::Group<> builder = BLayoutBuilder::Group<>(this, B_VERTICAL);

	BLayoutBuilder::Group<>::GridBuilder gridBuilder = builder.AddGrid(0.0, B_USE_DEFAULT_SPACING);

	gridBuilder.Add(fPathControl->CreateLabelLayoutItem(), 0, 0)
		.Add(fPathControl->CreateTextViewLayoutItem(), 1, 0);
	gridBuilder.Add(fSizeControl->CreateLabelLayoutItem(), 0, 1)
		.Add(fSizeControl->CreateTextViewLayoutItem(), 1, 1);

	BButton* okButton;
	builder.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(new BButton(B_TRANSLATE("Cancel"), new BMessage(B_CANCEL)))
			.Add(okButton = new BButton(B_TRANSLATE("OK"), new BMessage(kMsgOk)))
		.End()
		.SetInsets(B_USE_DEFAULT_SPACING);

	SetDefaultButton(okButton);
}


CreateRamDiskPanel::~CreateRamDiskPanel()
{
}

void CreateRamDiskPanel::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case B_OK:
		break;
	case B_CANCEL:
		PostMessage(B_QUIT_REQUESTED);
		break;
	}
}

