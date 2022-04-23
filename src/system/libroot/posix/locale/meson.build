
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/locale/posix_locale.o',
	[
		'LocaleBackend.cpp',
		'LocaleData.cpp',
		'LocaleDataBridge.cpp',
		'LocaleInternal.cpp',
		'ThreadLocale.cpp',
		'ctype.cpp',
		'ctype_l.cpp',
		'locale_t.cpp',
		'localeconv.cpp',
		'nl_langinfo.cpp',
		'setlocale.cpp',
		'wctype.cpp',
		'wctype_l.cpp',
	],
	include_directories: [
		'headers/private',
		'headers/private/libroot',
		'headers/private/libroot/locale',
		'headers/private/libroot/time',
		'headers/private/system',
	],
	include_directories: [
		'.',
	],
)
