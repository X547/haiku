/*
 * Copyright 2010-2014, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */

#include <DateTimeFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>
#include <TimeZone.h>


BDateTimeFormat::BDateTimeFormat(const BLocale* locale)
	: BFormat(locale)
{
}


BDateTimeFormat::BDateTimeFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
	: BFormat(language, conventions)
{
}


BDateTimeFormat::BDateTimeFormat(const BDateTimeFormat &other)
	: BFormat(other)
{
}


BDateTimeFormat::~BDateTimeFormat()
{
}


void
BDateTimeFormat::SetDateTimeFormat(BDateFormatStyle dateStyle,
	BTimeFormatStyle timeStyle, int32 elements)
{
}


// #pragma mark - Formatting


ssize_t
BDateTimeFormat::Format(char* target, size_t maxSize, time_t time,
	BDateFormatStyle dateStyle, BTimeFormatStyle timeStyle) const
{
	return strlcpy(target, "stub", maxSize);
}


status_t
BDateTimeFormat::Format(BString& target, const time_t time,
	BDateFormatStyle dateStyle, BTimeFormatStyle timeStyle,
	const BTimeZone* timeZone) const
{
	target = "stub";
	return B_OK;
}
