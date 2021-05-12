/*
 * Copyright 2010-2014, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Tappe <zooey@hirschkaefer.de>
 *		Adrien Desutugues <pulkomandy@pulkomandy.tk>
 */

#include <DateFormat.h>


BDateFormat::BDateFormat(const BLocale* locale)
	: BFormat(locale)
{
}


BDateFormat::BDateFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


BDateFormat::BDateFormat(const BDateFormat &other)
	: BFormat(other)
{
}


BDateFormat::~BDateFormat()
{
}


status_t
BDateFormat::GetMonthName(int month, BString& outName,
	const BDateFormatStyle style) const
{
	outName = "stub";
	return B_OK;
}


status_t
BDateFormat::GetDayName(int day, BString& outName,
	const BDateFormatStyle style) const
{
	outName = "stub";
	return B_OK;
}

status_t BDateFormat::Format(BString& string, const time_t time,
	const BDateFormatStyle style,
	const BTimeZone* timeZone) const
{
	string = "stub";
	return B_OK;
}

ssize_t BDateFormat::Format(char* string, const size_t maxSize,
	const time_t time,
	const BDateFormatStyle style) const
{
	return strlcpy(string, "stub", maxSize);
}

status_t BDateFormat::GetStartOfWeek(BWeekday* weekday) const 
{
	*weekday = B_WEEKDAY_SUNDAY;
	return B_OK;
}
