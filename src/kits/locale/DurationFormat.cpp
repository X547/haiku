/*
 * Copyright 2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */


#include <DurationFormat.h>

#include <new>

#include <Locale.h>
#include <LocaleRoster.h>
#include <TimeZone.h>

#include <TimeZonePrivate.h>


BDurationFormat::BDurationFormat(const BLanguage& language,
	const BFormattingConventions& conventions,
	const BString& separator, const time_unit_style style)
	:
	Inherited(language, conventions),
	fSeparator(separator),
	fTimeUnitFormat(language, conventions, style)
{
}


BDurationFormat::BDurationFormat(const BString& separator,
	const time_unit_style style)
	:
	Inherited(),
	fSeparator(separator),
	fTimeUnitFormat(style)
{
}


BDurationFormat::BDurationFormat(const BDurationFormat& other)
	:
	Inherited(other),
	fSeparator(other.fSeparator),
	fTimeUnitFormat(other.fTimeUnitFormat),
	fCalendar(NULL)
{
}


BDurationFormat::~BDurationFormat()
{
}


void
BDurationFormat::SetSeparator(const BString& separator)
{
	fSeparator = separator;
}


status_t
BDurationFormat::SetTimeZone(const BTimeZone* timeZone)
{
	return B_OK;
}


status_t
BDurationFormat::Format(BString& buffer, const bigtime_t startValue,
	const bigtime_t stopValue) const
{
	buffer = "stub";
	return B_OK;
}
