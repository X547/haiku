
shared_library(
	'objects/haiku/x86_64/release/system/libroot/add-ons/icu/libroot-addon-icu.so',
	[
		'ICUCategoryData.cpp',
		'ICUCollateData.cpp',
		'ICUCtypeData.cpp',
		'ICULocaleBackend.cpp',
		'ICULocaleconvData.cpp',
		'ICUMessagesData.cpp',
		'ICUMonetaryData.cpp',
		'ICUNumericData.cpp',
		'ICUThreadLocalStorageValue.cpp',
		'ICUTimeConversion.cpp',
		'ICUTimeData.cpp',
	],
	include_directories: [
		'headers/private',
		'headers/private/kernel',
		'headers/private/libroot',
		'headers/private/libroot/locale',
		'headers/private/libroot/time',
		'headers/private/shared',
	],
	include_directories: [
		'.',
	],
	link_with: [
		'objects/haiku/x86_64/release/kits/support/libreferenceable.a',
	],
)
