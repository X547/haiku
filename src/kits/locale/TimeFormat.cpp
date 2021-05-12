/*
 * Copyright 2010-2014, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */

#include <TimeFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <DateTime.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>
#include <TimeZone.h>

#include <vector>


BTimeFormat::BTimeFormat()
	: BFormat()
{
}


BTimeFormat::BTimeFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


BTimeFormat::BTimeFormat(const BTimeFormat &other)
	: BFormat(other)
{
}


BTimeFormat::~BTimeFormat()
{
}


void
BTimeFormat::SetTimeFormat(BTimeFormatStyle style,
	const BString& format)
{
}


// #pragma mark - Formatting


ssize_t
BTimeFormat::Format(char* string, size_t maxSize, time_t time,
	BTimeFormatStyle style) const
{
	return strlcpy(string, "stub", maxSize);
}


status_t
BTimeFormat::Format(BString& string, const time_t time,
	const BTimeFormatStyle style, const BTimeZone* timeZone) const
{
	string = "stub";
	return B_OK;
}


status_t
BTimeFormat::Format(BString& string, int*& fieldPositions, int& fieldCount,
	time_t time, BTimeFormatStyle style) const
{
	string = "stub";
	return B_OK;
}


status_t
BTimeFormat::GetTimeFields(BDateElement*& fields, int& fieldCount,
	BTimeFormatStyle style) const
{
	return B_OK;
}


status_t
BTimeFormat::Parse(BString source, BTimeFormatStyle style, BTime& output)
{
	return B_OK;
}
