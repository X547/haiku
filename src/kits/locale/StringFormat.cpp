/*
 * Copyright 2014-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *		John Scipione, jscipione@gmail.com
 */

#include <StringFormat.h>

#include <Autolock.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>


BStringFormat::BStringFormat(const BLanguage& language, const BString pattern)
	: BFormat(language, BFormattingConventions())
{
}


BStringFormat::BStringFormat(const BString pattern)
	: BFormat()
{
}


BStringFormat::~BStringFormat()
{
}


status_t
BStringFormat::InitCheck()
{
	return fInitStatus;
}


status_t
BStringFormat::Format(BString& output, const int64 arg) const
{
	output = "stub";
	return B_OK;
}
