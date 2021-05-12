/*
 * Copyright 2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Adrien Destugues <pulkomandy@gmail.com>
 *		Oliver Tappe <zooey@hirschkaefer.de>
 */


#include <TimeUnitFormat.h>

#include <new>

#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>


BTimeUnitFormat::BTimeUnitFormat(const time_unit_style style)
	: Inherited()
{
}


BTimeUnitFormat::BTimeUnitFormat(const BLanguage& language,
	const BFormattingConventions& conventions,
	const time_unit_style style)
	: Inherited(language, conventions)
{
}


BTimeUnitFormat::BTimeUnitFormat(const BTimeUnitFormat& other)
	:
	Inherited(other),
	fFormatter(NULL)
{
}


BTimeUnitFormat::~BTimeUnitFormat()
{
}


status_t
BTimeUnitFormat::Format(BString& buffer, const int32 value,
	const time_unit_element unit) const
{
	buffer = "stub";
	return B_OK;
}
