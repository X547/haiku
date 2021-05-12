/*
 * Copyright 2003-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2009-2010, Adrien Destugues, pulkomandy@gmail.com.
 * Copyright 2010-2011, Oliver Tappe <zooey@hirschkaefer.de>.
 * Distributed under the terms of the MIT License.
 */


#include <FormattingConventions.h>

#include <AutoDeleter.h>
#include <IconUtils.h>
#include <List.h>
#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <Resources.h>
#include <String.h>
#include <UnicodeChar.h>

#include <map>
#include <new>
#include <stdarg.h>
#include <stdlib.h>


// #pragma mark - BFormattingConventions


enum ClockHoursState {
	CLOCK_HOURS_UNSET = 0,
	CLOCK_HOURS_24,
	CLOCK_HOURS_12
};


BFormattingConventions::BFormattingConventions(const char* id)
	:
	fCachedUse24HourClock(CLOCK_HOURS_UNSET),
	fExplicitUse24HourClock(CLOCK_HOURS_UNSET),
	fUseStringsFromPreferredLanguage(false),
	fICULocale(NULL)
{
}


BFormattingConventions::BFormattingConventions(
	const BFormattingConventions& other)
	:
	fCachedNumericFormat(other.fCachedNumericFormat),
	fCachedMonetaryFormat(other.fCachedMonetaryFormat),
	fCachedUse24HourClock(other.fCachedUse24HourClock),
	fExplicitNumericFormat(other.fExplicitNumericFormat),
	fExplicitMonetaryFormat(other.fExplicitMonetaryFormat),
	fExplicitUse24HourClock(other.fExplicitUse24HourClock),
	fUseStringsFromPreferredLanguage(other.fUseStringsFromPreferredLanguage),
	fICULocale(NULL)
{
}


BFormattingConventions::BFormattingConventions(const BMessage* archive)
	:
	fCachedUse24HourClock(CLOCK_HOURS_UNSET),
	fExplicitUse24HourClock(CLOCK_HOURS_UNSET),
	fUseStringsFromPreferredLanguage(false)
{
}


BFormattingConventions&
BFormattingConventions::operator=(const BFormattingConventions& other)
{
	return *this;
}


BFormattingConventions::~BFormattingConventions()
{
}


bool
BFormattingConventions::operator==(const BFormattingConventions& other) const
{
	if (this == &other)
		return true;

	return fExplicitNumericFormat == other.fExplicitNumericFormat
		&& fExplicitMonetaryFormat == other.fExplicitMonetaryFormat
		&& fExplicitUse24HourClock == other.fExplicitUse24HourClock
		&& fUseStringsFromPreferredLanguage
			== other.fUseStringsFromPreferredLanguage;
}


bool
BFormattingConventions::operator!=(const BFormattingConventions& other) const
{
	return !(*this == other);
}


status_t
BFormattingConventions::Archive(BMessage* archive, bool deep) const
{
	status_t status = B_OK;
	for (int s = 0; s < B_DATE_FORMAT_STYLE_COUNT && status == B_OK; ++s) {
		status = archive->AddString("dateFormat", fExplicitDateFormats[s]);
		if (status == B_OK)
			status = archive->AddString("timeFormat", fExplicitTimeFormats[s]);
	}
	if (status == B_OK)
		status = archive->AddInt8("use24HourClock", fExplicitUse24HourClock);
	if (status == B_OK) {
		status = archive->AddBool("useStringsFromPreferredLanguage",
			fUseStringsFromPreferredLanguage);
	}

	return status;
}
