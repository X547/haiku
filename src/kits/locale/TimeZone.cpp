/*
 * Copyright (c) 2010, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Adrien Destugues <pulkomandy@pulkomandy.ath.cx>
 * 		Oliver Tappe <zooey@hirschkaefer.de>
 */


//#include <unicode/uversion.h>
#include <TimeZone.h>

#include <new>
/*
#include <unicode/locid.h>
#include <unicode/timezone.h>
#include <ICUWrapper.h>
*/
#include <Language.h>


// U_NAMESPACE_USE


const char* BTimeZone::kNameOfGmtZone = "GMT";


static const BString skEmptyString;


static const uint32 skNameField 					= 1U << 0;
static const uint32 skDaylightSavingNameField 		= 1U << 1;
static const uint32 skShortNameField 				= 1U << 2;
static const uint32 skShortDaylightSavingNameField 	= 1U << 3;
static const uint32 skLongGenericNameField 			= 1U << 4;
static const uint32 skGenericLocationNameField 		= 1U << 5;
static const uint32 skShortCommonlyUsedNameField	= 1U << 6;
static const uint32 skSupportsDaylightSavingField   = 1U << 7;
static const uint32 skOffsetFromGMTField			= 1U << 8;


BTimeZone::BTimeZone(const char* zoneID, const BLanguage* language)
	:
	fICUTimeZone(NULL),
	fICULocale(NULL),
	fInitStatus(B_NO_INIT),
	fInitializedFields(0)
{
	SetTo(zoneID, language);
}


BTimeZone::BTimeZone(const BTimeZone& other)
	:
	fICUTimeZone(NULL),
	fICULocale(NULL),
	fInitStatus(other.fInitStatus),
	fInitializedFields(other.fInitializedFields),
	fZoneID(other.fZoneID),
	fName(other.fName),
	fDaylightSavingName(other.fDaylightSavingName),
	fShortName(other.fShortName),
	fShortDaylightSavingName(other.fShortDaylightSavingName),
	fOffsetFromGMT(other.fOffsetFromGMT),
	fSupportsDaylightSaving(other.fSupportsDaylightSaving)
{
}


BTimeZone::~BTimeZone()
{
}


BTimeZone& BTimeZone::operator=(const BTimeZone& source)
{
	fInitStatus = source.fInitStatus;
	fInitializedFields = source.fInitializedFields;
	fZoneID = source.fZoneID;
	fName = source.fName;
	fDaylightSavingName = source.fDaylightSavingName;
	fShortName = source.fShortName;
	fShortDaylightSavingName = source.fShortDaylightSavingName;
	fOffsetFromGMT = source.fOffsetFromGMT;
	fSupportsDaylightSaving = source.fSupportsDaylightSaving;

	return *this;
}


const BString&
BTimeZone::ID() const
{
	return fZoneID;
}


const BString&
BTimeZone::Name() const
{
	if ((fInitializedFields & skNameField) == 0) {
		fName = "stub";
		fInitializedFields |= skNameField;
	}

	return fName;
}


const BString&
BTimeZone::DaylightSavingName() const
{
	if ((fInitializedFields & skDaylightSavingNameField) == 0) {
		fDaylightSavingName = "stub";
		fInitializedFields |= skDaylightSavingNameField;
	}

	return fDaylightSavingName;
}


const BString&
BTimeZone::ShortName() const
{
	if ((fInitializedFields & skShortNameField) == 0) {
		fShortName = "stub";
		fInitializedFields |= skShortNameField;
	}

	return fShortName;
}


const BString&
BTimeZone::ShortDaylightSavingName() const
{
	if ((fInitializedFields & skShortDaylightSavingNameField) == 0) {
		fShortDaylightSavingName = "stub";
		fInitializedFields |= skShortDaylightSavingNameField;
	}

	return fShortDaylightSavingName;
}


int
BTimeZone::OffsetFromGMT() const
{
	if ((fInitializedFields & skOffsetFromGMTField) == 0) {
		fOffsetFromGMT = 0;
		fInitializedFields |= skOffsetFromGMTField;
	}

	return fOffsetFromGMT;
}


bool
BTimeZone::SupportsDaylightSaving() const
{
	if ((fInitializedFields & skSupportsDaylightSavingField) == 0) {
		fSupportsDaylightSaving = false;
		fInitializedFields |= skSupportsDaylightSavingField;
	}

	return fSupportsDaylightSaving;
}


status_t
BTimeZone::InitCheck() const
{
	return fInitStatus;
}


status_t
BTimeZone::SetLanguage(const BLanguage* language)
{
	return SetTo(fZoneID, language);
}


status_t
BTimeZone::SetTo(const char* zoneID, const BLanguage* language)
{
	fInitializedFields = 0;
/*
	if (zoneID == NULL || zoneID[0] == '\0')
		fICUTimeZone = NULL;
	else
		fICUTimeZone = NULL;

	if (fICUTimeZone == NULL) {
		fInitStatus = B_NAME_NOT_FOUND;
		return fInitStatus;
	}

	if (language != NULL) {
		fICULocale = new Locale(language->Code());
		if (fICULocale == NULL) {
			fInitStatus = B_NO_MEMORY;
			return fInitStatus;
		}
	}

	UnicodeString unicodeString;
	fICUTimeZone->getID(unicodeString);
	BStringByteSink sink(&fZoneID);
	unicodeString.toUTF8(sink);
*/
	fInitStatus = B_OK;

	return fInitStatus;
}
