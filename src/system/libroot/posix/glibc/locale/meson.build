
static_library(
	'objects/haiku/x86_64/release/system/libroot/posix/glibc/locale/posix_gnu_locale.o',
	[
		'C-collate.c',
		'C-ctype.c',
		'C-messages.c',
		'C-monetary.c',
		'C-numeric.c',
		'C-time.c',
		'C_name.c',
		'coll-lookup.c',
		'global-locale.c',
		'lc-collate.c',
		'lc-ctype.c',
		'lc-messages.c',
		'lc-monetary.c',
		'lc-numeric.c',
		'xlocale.c',
	],
	cpp_args: [
		'LOCALEDIR='"/share/locale"'',
		'USE_IN_LIBIO',
		'_GNU_SOURCE',
	],
	include_directories: [
		'headers/private',
		'src/system/libroot/posix/glibc',
		'src/system/libroot/posix/glibc/ctype',
		'src/system/libroot/posix/glibc/include',
		'src/system/libroot/posix/glibc/include/arch/generic',
		'src/system/libroot/posix/glibc/include/arch/x86_64',
		'src/system/libroot/posix/glibc/locale',
	],
	include_directories: [
		'.',
	],
)
