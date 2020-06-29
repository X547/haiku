/*
 * Copyright 2001-2006, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar R. Adema
 */


#include "PrintServerApp.h"

#include <stdio.h>

#include <Catalog.h>
#include <Locale.h>
#include <PropertyInfo.h>

#include "Transport.h"
#include "Printer.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintServerApp Scripting"


static property_info prop_list[] = {
	{ "ActivePrinter", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		B_TRANSLATE_MARK("Retrieve or select the active printer") },
	{ "Printer", { B_GET_PROPERTY }, { B_INDEX_SPECIFIER, B_NAME_SPECIFIER,
		B_REVERSE_INDEX_SPECIFIER },
		B_TRANSLATE_MARK("Retrieve a specific printer") },
	{ "Printer", { B_CREATE_PROPERTY }, { B_DIRECT_SPECIFIER },
		B_TRANSLATE_MARK("Create a new printer") },
	{ "Printer", { B_DELETE_PROPERTY }, { B_INDEX_SPECIFIER, B_NAME_SPECIFIER,
		B_REVERSE_INDEX_SPECIFIER },
		B_TRANSLATE_MARK("Delete a specific printer") },
	{ "Printer", { B_COUNT_PROPERTIES }, { B_DIRECT_SPECIFIER },
		B_TRANSLATE_MARK("Return the number of available printers") },
	{ "Transport", { B_GET_PROPERTY }, { B_INDEX_SPECIFIER, B_NAME_SPECIFIER,
		B_REVERSE_INDEX_SPECIFIER },
		B_TRANSLATE_MARK("Retrieve a specific transport") },
	{ "Transport", { B_COUNT_PROPERTIES }, { B_DIRECT_SPECIFIER },
		B_TRANSLATE_MARK("Return the number of available transports") },
	{ "UseConfigWindow", { B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		B_TRANSLATE_MARK("Show configuration window") },

	{ 0 } // terminate list
};


void
PrintServerApp::HandleScriptingCommand(BMessage* msg)
{
	BMessage reply(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (msg->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return Inherited::MessageReceived(msg);
	}

	BPropertyInfo propInfo(prop_list);
	switch (propInfo.FindMatch(msg, index, &specifier, what, property)) {
		case 0: // ActivePrinter: GET, SET
			switch (msg->what) {
				case B_GET_PROPERTY:
					err = reply.AddString("result", fDefaultPrinter
						? fDefaultPrinter->Name() : "");
					break;
				case B_SET_PROPERTY: {
					BString newActivePrinter;
					err = msg->FindString("data", &newActivePrinter);
					if (err >= B_OK)
						err = SelectPrinter(newActivePrinter.String());
					break;
				}
			}
			break;
		case 2: { // Printer: CREATE
			BString name, driver, transport, config;
			err = msg->FindString("name", &name);
			if (err >= B_OK)
				err = msg->FindString("driver", &driver);
			if (err >= B_OK)
				err = msg->FindString("transport", &transport);
			if (err >= B_OK)
				err = msg->FindString("config", &config);
			if (err >= B_OK)
				err = CreatePrinter(name, driver, "Local", transport, config);
			break;
		}
		case 3: { // Printer: DELETE
			Printer* printer = GetPrinterFromSpecifier(&specifier);
			if (printer == NULL)
				err = B_BAD_VALUE;
			if (err >= B_OK)
				err = printer->Remove();
			break;
		}
		case 4: // Printers: COUNT
			err = reply.AddInt32("result", Printer::CountPrinters());
			break;
		case 6: // Transports: COUNT
			err = reply.AddInt32("result", Transport::CountTransports());
			break;
		case 7: // UseConfigWindow: GET, SET
			switch (msg->what) {
				case B_GET_PROPERTY:
					err = reply.AddString("result", fUseConfigWindow ? "true"
						: "false");
					break;
				case B_SET_PROPERTY:
					bool useConfigWindow;
					err = msg->FindBool("data", &useConfigWindow);
					if (err >= B_OK)
						fUseConfigWindow = useConfigWindow;
					break;
			}
			break;
		default:
			return Inherited::MessageReceived(msg);
	}

	if (err < B_OK) {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddString("message", strerror(err));
	}

	reply.AddInt32("error", err);
	msg->SendReply(&reply);
}


Printer*
PrintServerApp::GetPrinterFromSpecifier(BMessage* msg)
{
	switch(msg->what) {
		case B_NAME_SPECIFIER:
		{
			BString name;
			if (msg->FindString("name", &name) == B_OK)
				return Printer::Find(name.String());
			break;
		}

		case B_INDEX_SPECIFIER:
		{
			int32 idx;
			if (msg->FindInt32("index", &idx) == B_OK)
				return Printer::At(idx);
			break;
		}

		case B_REVERSE_INDEX_SPECIFIER:
		{
			int32 idx;
			if (msg->FindInt32("index", &idx) == B_OK)
				return Printer::At(Printer::CountPrinters() - idx);
			break;
		}
	}

	return NULL;
}


Transport*
PrintServerApp::GetTransportFromSpecifier(BMessage* msg)
{
	switch(msg->what) {
		case B_NAME_SPECIFIER:
		{
			BString name;
			if (msg->FindString("name", &name) == B_OK)
				return Transport::Find(name);
			break;
		}

		case B_INDEX_SPECIFIER:
		{
			int32 idx;
			if (msg->FindInt32("index", &idx) == B_OK)
				return Transport::At(idx);
			break;
		}

		case B_REVERSE_INDEX_SPECIFIER:
		{
			int32 idx;
			if (msg->FindInt32("index", &idx) == B_OK)
				return Transport::At(Transport::CountTransports() - idx);
			break;
		}
	}

	return NULL;
}


BHandler*
PrintServerApp::ResolveSpecifier(BMessage* msg, int32 index, BMessage* spec,
								int32 form, const char* prop)
{
	BPropertyInfo prop_info(prop_list);
	BHandler* rc = NULL;

	int32 idx;
	switch( idx=prop_info.FindMatch(msg,0,spec,form,prop) ) {
		case B_ERROR:
			rc = Inherited::ResolveSpecifier(msg,index,spec,form,prop);
			break;

		case 1:
			// GET Printer [arg]
			if ((rc=GetPrinterFromSpecifier(spec)) == NULL) {
				BMessage reply(B_REPLY);
				reply.AddInt32("error", B_BAD_INDEX);
				msg->SendReply(&reply);
			}
			else
				msg->PopSpecifier();
			break;

		case 5:
			// GET Transport [arg]
			if ((rc=GetTransportFromSpecifier(spec)) == NULL) {
				BMessage reply(B_REPLY);
				reply.AddInt32("error", B_BAD_INDEX);
				msg->SendReply(&reply);
			}
			else
				msg->PopSpecifier();
			break;

		default:
			rc = this;
	}

	return rc;
}


status_t
PrintServerApp::GetSupportedSuites(BMessage* msg)
{
	msg->AddString("suites", "suite/vnd.Haiku-printserver");

	static bool localized = false;
	if (!localized) {
		localized = true;
		for (int i = 0; prop_list[i].name != NULL; i ++)
			prop_list[i].usage = B_TRANSLATE_NOCOLLECT(prop_list[i].usage);
	}

	BPropertyInfo prop_info(prop_list);
	msg->AddFlat("messages", &prop_info);

	return Inherited::GetSupportedSuites(msg);
}
