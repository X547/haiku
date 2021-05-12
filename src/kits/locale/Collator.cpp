/*
* Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
* Copyright 2010, Adrien Destugues <pulkomandy@pulkomandy.ath.cx>
* Distributed under the terms of the MIT License.
*/


#include <Collator.h>

#include <ctype.h>
#include <stdlib.h>

#include <new>
#include <typeinfo>

#include <UnicodeChar.h>
#include <String.h>
#include <Message.h>


BCollator::BCollator()
	:
	fIgnorePunctuation(true)
{
	// TODO: the collator construction will have to change; the default
	//	collator should be constructed by the Locale/LocaleRoster, so we
	//	only need a constructor where you specify all details

	SetStrength(B_COLLATE_TERTIARY);
}


BCollator::BCollator(const char* locale, int8 strength, bool ignorePunctuation)
	:
	fIgnorePunctuation(ignorePunctuation)
{
	SetStrength(strength);
}


BCollator::BCollator(BMessage* archive)
	:
	BArchivable(archive),
	fICUCollator(NULL),
	fIgnorePunctuation(true)
{
}


BCollator::BCollator(const BCollator& other)
	:
	fICUCollator(NULL)
{
	*this = other;
}


BCollator::~BCollator()
{
}


BCollator& BCollator::operator=(const BCollator& source)
{
	return *this;
}


void
BCollator::SetIgnorePunctuation(bool ignore)
{
	fIgnorePunctuation = ignore;
}


bool
BCollator::IgnorePunctuation() const
{
	return fIgnorePunctuation;
}


status_t
BCollator::SetNumericSorting(bool enable)
{
	return B_OK;
}


status_t
BCollator::GetSortKey(const char* string, BString* key) const
{
	return B_OK;
}


int
BCollator::Compare(const char* s1, const char* s2) const
{
	return 0;
}


status_t
BCollator::Archive(BMessage* archive, bool deep) const
{
	return B_OK;
}


BArchivable*
BCollator::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BCollator"))
		return new(std::nothrow) BCollator(archive);

	return NULL;
}


status_t
BCollator::SetStrength(int8 strength) const
{
	return B_OK;
}
